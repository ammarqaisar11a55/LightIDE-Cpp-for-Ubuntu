// Tests the LeetCode port against LeetRunner's own reference data: real problem
// payloads in tests/leetcode/problems and reference solutions in tests/leetcode/solutions.

#include "compiler/BuildProcess.h"
#include "leetcode/LeetCodeClient.h"
#include "leetcode/ProblemFolder.h"
#include "leetcode/QuestionParser.h"
#include "leetcode/SolutionFile.h"
#include "leetcode/TestSuiteRunner.h"

#include <QtNetwork>
#include <QtTest>

Q_DECLARE_METATYPE(BuildResult)
Q_DECLARE_METATYPE(LcRunReport)
Q_DECLARE_METATYPE(LeetCodeProblem)

namespace {

constexpr int timeoutMs = 60000;

QString fixturesRoot()
{
    return QStringLiteral(LIGHTIDE_LEETCODE_FIXTURES);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

LeetCodeProblem loadProblem(const QString &slug)
{
    const QByteArray data = readFile(fixturesRoot() + QStringLiteral("/problems/") + slug + QStringLiteral(".json"));
    return QuestionParser::parseQuestion(QJsonDocument::fromJson(data).object());
}

// Puts a reference solution into solution.cpp in place of the starter code.
void insertSolution(const ProblemFolder &folder, const QString &slug)
{
    QString source = QString::fromUtf8(readFile(folder.solutionPath()));
    const QString code = QString::fromUtf8(readFile(fixturesRoot() + QStringLiteral("/solutions/") + slug + QStringLiteral(".cpp")));
    const auto sections = SolutionFile::split(source);
    QVERIFY(sections.has_value());
    source = sections->preamble + SolutionFile::userSolutionMarker + QStringLiteral("\n\n") + code
             + QStringLiteral("\n") + SolutionFile::testHarnessMarker + QStringLiteral("\n") + sections->harness;
    writeFile(folder.solutionPath(), source.toUtf8());
}

struct Outcome
{
    BuildResult build;
    LcRunReport report;
    QStringList compilerOutput;
};

Outcome compileAndRun(const ProblemFolder &folder, int timeout = 5000)
{
    Outcome outcome;
    const ProblemMetadata metadata = folder.readMetadata();
    const QList<LcTestCase> tests = folder.readTests(metadata);
    LeetCodeRunSettings settings;
    settings.timeoutMs = timeout;
    settings.flags = {QStringLiteral("-O0")};

    BuildProcess build;
    QObject::connect(&build, &BuildProcess::outputLine, &build,
                     [&outcome](const QString &line) { outcome.compilerOutput << line; });
    QSignalSpy built(&build, &BuildProcess::finished);
    build.start(folder.makeBuildPlan(metadata, settings, false));
    if (built.isEmpty() && !built.wait(timeoutMs))
        return outcome;
    outcome.build = built.at(0).at(0).value<BuildResult>();
    if (!outcome.build.success)
        return outcome;
    folder.markBuilt(settings, false);

    TestSuiteRunner runner;
    QSignalSpy finished(&runner, &TestSuiteRunner::finished);
    runner.start(folder, metadata, tests, settings);
    if (finished.isEmpty())
        finished.wait(timeoutMs);
    outcome.report = finished.at(0).at(0).value<LcRunReport>();
    return outcome;
}

QStringList allFixtureSlugs()
{
    QStringList slugs;
    const QStringList files = QDir(fixturesRoot() + QStringLiteral("/problems")).entryList({QStringLiteral("*.json")});
    for (const QString &file : files)
        slugs << QFileInfo(file).completeBaseName();
    return slugs;
}

} // namespace

class TestLeetCode : public QObject
{
    Q_OBJECT

private:
    // One fixture's solution, in a fresh folder, with a chosen body.
    ProblemFolder twoSumWithBody(const QTemporaryDir &dir, const QByteArray &body)
    {
        const ProblemFolder folder(dir.filePath("two-sum"));
        folder.create(loadProblem("two-sum"), false);
        QByteArray source = readFile(folder.solutionPath());
        source.replace("// TODO: write your solution", body);
        writeFile(folder.solutionPath(), source);
        return folder;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<BuildResult>();
        qRegisterMetaType<LcRunReport>();
        qRegisterMetaType<LeetCodeProblem>();
        QVERIFY(QFileInfo(fixturesRoot() + "/problems/two-sum.json").exists());
    }

    // --- References ----------------------------------------------------------

    void parsesProblemReferences_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("slug");
        QTest::newRow("slug") << "two-sum" << "two-sum";
        QTest::newRow("url") << "https://leetcode.com/problems/two-sum/" << "two-sum";
        QTest::newRow("sub page") << "https://leetcode.com/problems/two-sum/description/?a=1" << "two-sum";
        QTest::newRow("cn") << "https://leetcode.cn/problems/two-sum/" << "two-sum";
        QTest::newRow("title") << "Two Sum" << "two-sum";
        QTest::newRow("numbered title") << "1. Two Sum" << "two-sum";
        QTest::newRow("apostrophe") << "Pascal's Triangle" << "pascals-triangle";
        QTest::newRow("spaces") << "  two-sum  " << "two-sum";
    }

    void parsesProblemReferences()
    {
        QFETCH(QString, input);
        QFETCH(QString, slug);
        QCOMPARE(QuestionParser::parseProblemReference(input), slug);
    }

    void rejectsBadReferences()
    {
        for (const char *input : {"", "1", "https://leetcode.com/contest/", "!!!"}) {
            QVERIFY_THROWS_EXCEPTION(LcError, QuestionParser::parseProblemReference(input));
        }
    }

    // --- Values --------------------------------------------------------------

    void parsesValues()
    {
        QCOMPARE(toLeetCodeText(LcValueParser::parse("[1, 2,3,]")), QString("[1,2,3]"));
        QCOMPARE(toLeetCodeText(LcValueParser::parse("'abc'")), QString("\"abc\""));
        QCOMPARE(toLeetCodeText(LcValueParser::parse("[null,True,#,_]")), QString("[null,true,#,_]"));
        QCOMPARE(LcValueParser::parse("9223372036854775807").text, QString("9223372036854775807"));
        QCOMPARE(LcValueParser::parse("2.00000").text, QString("2.00000"));
        QCOMPARE(LcValueParser::parse("\"a\\u0041\\n\"").text, QString("aA\n"));
        QVERIFY(!LcValueParser::tryParse("[1,2"));
        QVERIFY(!LcValueParser::tryParse("1 2"));
    }

    void parsesInPlaceOutputs()
    {
        const LcValue value = QuestionParser::parseOutputText("2, nums = [2,2,_,_]");
        QVERIFY(value.isObject());
        QCOMPARE(value.entry("return")->text, QString("2"));
        QCOMPARE(toLeetCodeText(value), QString("2, nums = [2,2,_,_]"));
    }

    // --- Parsing real problems ----------------------------------------------

    void everyFixtureParses_data()
    {
        QTest::addColumn<QString>("slug");
        const QStringList slugs = allFixtureSlugs();
        for (const QString &slug : slugs)
            QTest::newRow(slug.toUtf8().constData()) << slug;
    }

    void everyFixtureParses()
    {
        QFETCH(QString, slug);
        const LeetCodeProblem problem = loadProblem(slug);
        QCOMPARE(problem.slug, slug);
        QVERIFY(!problem.starterCode.isEmpty());
        QVERIFY(!problem.examples.isEmpty());

        QTemporaryDir dir;
        const ProblemFolder folder(dir.filePath(slug));
        folder.create(problem, false);
        QVERIFY(folder.isProblem());
        QVERIFY(QFileInfo::exists(folder.headerPath()));

        const ProblemMetadata metadata = folder.readMetadata();
        QCOMPARE(folder.readTests(metadata).size(), problem.examples.size());
    }

    // --- Fidelity to the extension ------------------------------------------

    void generatedFilesMatchTheExtensionByteForByte_data()
    {
        QTest::addColumn<QString>("slug");
        const QStringList slugs = QDir(fixturesRoot() + QStringLiteral("/generated"))
                                      .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QVERIFY(!slugs.isEmpty());
        for (const QString &slug : slugs)
            QTest::newRow(slug.toUtf8().constData()) << slug;
    }

    void generatedFilesMatchTheExtensionByteForByte()
    {
        QFETCH(QString, slug);
        const QString reference = fixturesRoot() + QStringLiteral("/generated/") + slug;
        QTemporaryDir dir;
        const ProblemFolder folder(dir.filePath(slug));
        folder.create(loadProblem(slug), false);

        // The reference has a sample solution in the user section, so the
        // generated parts are compared: everything around that section.
        const auto expected = SolutionFile::split(QString::fromUtf8(readFile(reference + "/solution.cpp")));
        const auto actual = SolutionFile::split(QString::fromUtf8(readFile(folder.solutionPath())));
        QVERIFY(expected && actual);
        QCOMPARE(actual->preamble, expected->preamble);
        QCOMPARE(actual->harness, expected->harness);

        QCOMPARE(readFile(folder.testsPath()), readFile(reference + "/tests.json"));
        QCOMPARE(readFile(folder.headerPath()), readFile(reference + "/.lc/leetcode.hpp"));

        const QJsonObject metadata = QJsonDocument::fromJson(readFile(reference + "/metadata.json")).object();
        const ProblemMetadata ours = folder.readMetadata();
        QCOMPARE(ours.signatureHash, metadata.value("signatureHash").toString());
        QCOMPARE(ours.support, metadata.value("support").toString());
        QCOMPARE(OutputComparator::modeName(ours.comparison), metadata.value("comparison").toString());
    }

    void readsFoldersTheExtensionWrote()
    {
        const QStringList slugs = QDir(fixturesRoot() + QStringLiteral("/generated"))
                                      .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &slug : slugs) {
            const ProblemFolder folder(fixturesRoot() + QStringLiteral("/generated/") + slug);
            QVERIFY2(folder.isProblem(), qPrintable(slug));
            const ProblemMetadata metadata = folder.readMetadata();
            QCOMPARE(metadata.slug, slug);
            QVERIFY(!folder.readTests(metadata).isEmpty());
        }
    }

    // --- The full pipeline --------------------------------------------------

    void referenceSolutionsPassEveryExample_data()
    {
        QTest::addColumn<QString>("slug");
        const QStringList files = QDir(fixturesRoot() + QStringLiteral("/solutions")).entryList({QStringLiteral("*.cpp")});
        QVERIFY(files.size() >= 20);
        for (const QString &file : files)
            QTest::newRow(QFileInfo(file).completeBaseName().toUtf8().constData()) << QFileInfo(file).completeBaseName();
    }

    void referenceSolutionsPassEveryExample()
    {
        QFETCH(QString, slug);
        QTemporaryDir dir;
        const ProblemFolder folder(dir.filePath(slug));
        folder.create(loadProblem(slug), false);
        insertSolution(folder, slug);

        const Outcome outcome = compileAndRun(folder);
        QVERIFY2(outcome.build.success, qPrintable(outcome.compilerOutput.join("\n").left(800)));
        QVERIFY(outcome.report.error.isEmpty());
        QVERIFY(outcome.report.total > 0);
        for (const LcTestResult &test : outcome.report.tests) {
            QVERIFY2(test.status == LcTestStatus::Pass,
                     qPrintable(QString("%1: expected %2, got %3 %4").arg(test.name, test.expected.value_or("?"),
                                                                          test.actual.value_or("?"), test.message)));
        }
        QCOMPARE(outcome.report.passed, outcome.report.total);
    }

    void reportsAWrongAnswer()
    {
        QTemporaryDir dir;
        const Outcome outcome = compileAndRun(twoSumWithBody(dir, "return {7, 7};"));
        QVERIFY(outcome.report.failed);
        QCOMPARE(outcome.report.passed, 0);
        QCOMPARE(outcome.report.tests.first().status, LcTestStatus::Fail);
        QCOMPARE(*outcome.report.tests.first().actual, QString("[7,7]"));
        QCOMPARE(*outcome.report.tests.first().expected, QString("[0,1]"));
    }

    void reportsCompileErrors()
    {
        QTemporaryDir dir;
        const Outcome outcome = compileAndRun(twoSumWithBody(dir, "this does not compile"));
        QVERIFY(!outcome.build.success);
        QVERIFY(outcome.build.errors >= 1);
    }

    void reportsARuntimeCrashPerTest()
    {
        QTemporaryDir dir;
        const Outcome outcome = compileAndRun(twoSumWithBody(dir, "*(volatile int *)0 = 1; return {};"));
        QCOMPARE(outcome.report.total, 3);
        QCOMPARE(outcome.report.tests.first().status, LcTestStatus::RuntimeError);
        QVERIFY(outcome.report.tests.first().message.contains("SIGSEGV"));
    }

    void reportsUncaughtExceptions()
    {
        QTemporaryDir dir;
        const Outcome outcome = compileAndRun(twoSumWithBody(dir, "throw std::runtime_error(\"boom\");"));
        QCOMPARE(outcome.report.tests.first().status, LcTestStatus::RuntimeError);
        QVERIFY(outcome.report.tests.first().message.contains("boom"));
    }

    void timesOutAnInfiniteLoop()
    {
        QTemporaryDir dir;
        const Outcome outcome = compileAndRun(twoSumWithBody(dir, "for (;;) {} return {};"), 300);
        QCOMPARE(outcome.report.total, 3);
        QCOMPARE(outcome.report.tests.first().status, LcTestStatus::Timeout);
        QVERIFY(outcome.report.failed);
    }

    void stopsAnOutputFlood()
    {
        QTemporaryDir dir;
        const Outcome outcome = compileAndRun(twoSumWithBody(dir, "for (;;) cout << \"xxxxxxxxxxxxxxxxxxxx\\n\"; return {};"));
        QCOMPARE(outcome.report.tests.first().status, LcTestStatus::OutputLimit);
    }

    void capturesStdoutPerTest()
    {
        QTemporaryDir dir;
        const Outcome outcome = compileAndRun(twoSumWithBody(dir, "cout << \"debug print\" << endl; cerr << \"on stderr\" << endl; return {0, 1};"));
        const LcTestResult &first = outcome.report.tests.first();
        QCOMPARE(first.status, LcTestStatus::Pass);
        QCOMPARE(first.stdoutText, QString("debug print"));
        QCOMPARE(first.stderrText, QString("on stderr"));
        QVERIFY(first.timeMs.has_value());
    }

    void runsOnlyTheChosenTest()
    {
        QTemporaryDir dir;
        const ProblemFolder folder = twoSumWithBody(dir, "return {0, 1};");
        Outcome outcome = compileAndRun(folder);
        QVERIFY(outcome.build.success);

        const ProblemMetadata metadata = folder.readMetadata();
        TestSuiteRunner runner;
        QSignalSpy finished(&runner, &TestSuiteRunner::finished);
        runner.start(folder, metadata, folder.readTests(metadata), LeetCodeRunSettings{}, {2});
        QVERIFY(finished.wait(timeoutMs));
        const LcRunReport report = finished.at(0).at(0).value<LcRunReport>();
        QCOMPARE(report.total, 1);
        QCOMPARE(report.tests.first().id, 2);
    }

    void cachesBuildsUntilTheCodeChanges()
    {
        QTemporaryDir dir;
        const ProblemFolder folder = twoSumWithBody(dir, "return {0, 1};");
        QVERIFY(compileAndRun(folder).build.success);

        const ProblemMetadata metadata = folder.readMetadata();
        LeetCodeRunSettings settings;
        settings.flags = {QStringLiteral("-O0")};
        QVERIFY(folder.makeBuildPlan(metadata, settings, false).compileJobs.isEmpty());
        QCOMPARE(folder.makeBuildPlan(metadata, settings, true).compileJobs.size(), 1);

        writeFile(folder.solutionPath(), readFile(folder.solutionPath()) + "// changed\n");
        QCOMPARE(folder.makeBuildPlan(metadata, settings, false).compileJobs.size(), 1);
    }

    // --- Comparison ----------------------------------------------------------

    void comparesValues_data()
    {
        QTest::addColumn<QString>("actual");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("mode");
        QTest::addColumn<bool>("equal");

        QTest::newRow("same") << "[1,2]" << "[1,2]" << "exact" << true;
        QTest::newRow("order matters") << "[2,1]" << "[1,2]" << "exact" << false;
        QTest::newRow("any order") << "[2,1]" << "[1,2]" << "unordered" << true;
        QTest::newRow("nested order matters") << "[[2,1]]" << "[[1,2]]" << "unordered" << false;
        QTest::newRow("deep any order") << "[[2,1],[3]]" << "[[3],[1,2]]" << "unordered-deep" << true;
        QTest::newRow("multiset") << "[1,1,2]" << "[1,2,2]" << "unordered" << false;
        QTest::newRow("float tolerance") << "2.000001" << "2.00000" << "exact" << true;
        QTest::newRow("float differs") << "2.1" << "2.0" << "exact" << false;
        QTest::newRow("big integers are exact") << "9223372036854775807" << "9223372036854775806" << "exact" << false;
        QTest::newRow("integer forms") << "+05" << "5" << "exact" << true;
        QTest::newRow("empty and null") << "[]" << "null" << "exact" << true;
        QTest::newRow("in-place prefix") << "2, nums = [2,2,9,9]" << "2, nums = [2,2,_,_]" << "exact" << true;
        QTest::newRow("prefix differs") << "2, nums = [2,3,9,9]" << "2, nums = [2,2,_,_]" << "exact" << false;
        QTest::newRow("prefix any order") << "2, nums = [3,2,9]" << "2, nums = [2,3,_]" << "unordered" << true;
        QTest::newRow("negative sort") << "[-2,-10,5]" << "[5,-10,-2]" << "unordered" << true;
        QTest::newRow("strings") << "[\"b\",\"a\"]" << "[\"a\",\"b\"]" << "unordered" << true;
    }

    void comparesValues()
    {
        QFETCH(QString, actual);
        QFETCH(QString, expected);
        QFETCH(QString, mode);
        QFETCH(bool, equal);
        const ComparisonResult result = OutputComparator::compare(QuestionParser::parseOutputText(actual),
                                                                  QuestionParser::parseOutputText(expected),
                                                                  *OutputComparator::modeFromName(mode));
        QCOMPARE(result.equal, equal);
    }

    void hintsWhenOnlyTheOrderDiffers()
    {
        const auto result = OutputComparator::compare(LcValueParser::parse("[2,1]"), LcValueParser::parse("[1,2]"),
                                                      ComparisonMode::Exact);
        QVERIFY(!result.equal);
        QVERIFY(result.hint.contains("\"unordered\""));
    }

    // --- Solution file -------------------------------------------------------

    void extractsTheSubmission()
    {
        const QString source = QString("#include \".lc/leetcode.hpp\"\n\n%1\n\n"
                                       "class Solution {\n"
                                       "    void f() {\n"
                                       "#ifdef LC_LOCAL\n"
                                       "        cout << \"debug\";\n"
                                       "#endif\n"
                                       "        cerr << 1; // @local\n"
                                       "        g();\n"
                                       "    }\n"
                                       "};\n\n%2\nint main() {}\n")
                                   .arg(SolutionFile::userSolutionMarker, SolutionFile::testHarnessMarker);
        const QString submission = SolutionFile::extractSubmission(source);
        QCOMPARE(submission, QString("class Solution {\n    void f() {\n        g();\n    }\n};\n"));
    }

    void keepsTheElseBranchOfLocalBlocks()
    {
        QCOMPARE(SolutionFile::stripLocalOnly("#ifdef LC_LOCAL\na\n#else\nb\n#endif\nc"), QString("b\nc"));
        QCOMPARE(SolutionFile::stripLocalOnly("#ifdef OTHER\na\n#endif"), QString("#ifdef OTHER\na\n#endif"));
    }

    void restoresADeletedHarnessMarker()
    {
        const QString source = SolutionFile::userSolutionMarker + "\nclass Solution {};\n";
        const QString updated = SolutionFile::replaceHarness(source, "int main() {}\n");
        QVERIFY(updated.contains(SolutionFile::testHarnessMarker));
        QVERIFY(updated.contains("class Solution {};"));
        QVERIFY_THROWS_EXCEPTION(LcError, SolutionFile::replaceHarness("no markers", "x"));
    }

    void harnessKeepsCrlfLineEndings()
    {
        const QString source = SolutionFile::userSolutionMarker + "\r\ncode\r\n" + SolutionFile::testHarnessMarker + "\r\nold\r\n";
        const QString updated = SolutionFile::replaceHarness(source, "a\nb\n");
        QVERIFY(updated.endsWith("a\r\nb\r\n"));
    }

    // --- Folders -------------------------------------------------------------

    void keepsAnExistingSolutionWhenFetchedAgain()
    {
        QTemporaryDir dir;
        const ProblemFolder folder = twoSumWithBody(dir, "return {42};");
        folder.create(loadProblem("two-sum"), false);
        QVERIFY(readFile(folder.solutionPath()).contains("return {42};"));
    }

    void overwritingBacksTheSolutionUp()
    {
        QTemporaryDir dir;
        const ProblemFolder folder = twoSumWithBody(dir, "return {42};");
        folder.create(loadProblem("two-sum"), true);
        QVERIFY(!readFile(folder.solutionPath()).contains("return {42};"));
        QVERIFY(readFile(folder.supportDirectory() + "/solution.cpp.bak").contains("return {42};"));
    }

    void refreshKeepsCustomTestsAndTheComparisonChoice()
    {
        QTemporaryDir dir;
        const ProblemFolder folder(dir.filePath("two-sum"));
        const LeetCodeProblem problem = loadProblem("two-sum");
        folder.create(problem, false);

        ProblemMetadata metadata = folder.readMetadata();
        metadata.comparison = ComparisonMode::UnorderedDeep;
        folder.writeMetadata(metadata);

        QList<LcTestCase> tests = folder.readTests(metadata);
        LcTestCase custom = tests.first();
        custom.name = "Mine";
        custom.source = TestSource::Custom;
        tests.append(custom);
        folder.writeTests(metadata, tests);

        const QByteArray solution = readFile(folder.solutionPath());
        QVERIFY(!folder.refresh(problem));
        QCOMPARE(readFile(folder.solutionPath()), solution);

        const ProblemMetadata refreshed = folder.readMetadata();
        QCOMPARE(refreshed.comparison, ComparisonMode::UnorderedDeep);
        const QList<LcTestCase> merged = folder.readTests(refreshed);
        QCOMPARE(merged.size(), 4);
        QCOMPARE(merged.last().name, QString("Mine"));
        QCOMPARE(merged.last().id, 4);
    }

    void refreshNoticesASignatureChange()
    {
        QTemporaryDir dir;
        const ProblemFolder folder(dir.filePath("two-sum"));
        LeetCodeProblem problem = loadProblem("two-sum");
        folder.create(problem, false);
        problem.starterCode.replace("int target", "long target");
        QVERIFY(folder.refresh(problem));
    }

    void manualHarnessIsNeverRewritten()
    {
        const QStringList slugs = allFixtureSlugs();
        QVERIFY(slugs.contains("linked-list-cycle"));

        QTemporaryDir dir;
        const ProblemFolder folder(dir.filePath("linked-list-cycle"));
        folder.create(loadProblem("linked-list-cycle"), false);
        const ProblemMetadata metadata = folder.readMetadata();
        QVERIFY(metadata.manualHarness);

        const QString source = QString::fromUtf8(readFile(folder.solutionPath()));
        QCOMPARE(LeetCodeBuilder::syncHarness(source + "// mine\n", metadata, {}), source + "// mine\n");

        // Its skeleton compiles, and reports that it needs wiring.
        const Outcome outcome = compileAndRun(folder);
        QVERIFY2(outcome.build.success, qPrintable(outcome.compilerOutput.join("\n").left(800)));
        QCOMPARE(outcome.report.tests.first().status, LcTestStatus::RuntimeError);
    }

    void harnessFollowsTestsJson()
    {
        QTemporaryDir dir;
        const ProblemFolder folder = twoSumWithBody(dir, "return {0, 1};");
        const ProblemMetadata metadata = folder.readMetadata();

        QList<LcTestCase> tests = folder.readTests(metadata);
        LcTestCase custom = tests.first();
        custom.id = 4;
        custom.name = "Mine";
        custom.source = TestSource::Custom;
        custom.inputs[1] = LcValue::makeNumber("100");
        tests.append(custom);

        const QString source = QString::fromUtf8(readFile(folder.solutionPath()));
        const QString updated = LeetCodeBuilder::syncHarness(source, metadata, tests);
        QVERIFY(updated.contains("// Test Case 4 (custom): nums = [2,7,11,15], target = 100"));
        QVERIFY(updated.contains("return {0, 1};"));
    }

    void rejectsMalformedTestsFiles()
    {
        const QStringList keys = {"nums"};
        QVERIFY_THROWS_EXCEPTION(LcError, LeetCodeBuilder::parseTests("{", keys));
        QVERIFY_THROWS_EXCEPTION(LcError, LeetCodeBuilder::parseTests("{}", keys));
        QVERIFY_THROWS_EXCEPTION(LcError, LeetCodeBuilder::parseTests("[1]", keys));
        QVERIFY_THROWS_EXCEPTION(LcError, LeetCodeBuilder::parseTests("[{\"input\":{}}]", keys));
        QVERIFY_THROWS_EXCEPTION(LcError, LeetCodeBuilder::parseTests("[{\"input\":{\"nums\":[],\"x\":1}}]", keys));
        QCOMPARE(LeetCodeBuilder::parseTests("[{\"input\":{\"nums\":[1]}}]", keys).first().name, QString("Test 1"));
        QVERIFY(LeetCodeBuilder::parseTests("", keys).isEmpty());
    }

    // --- Network -------------------------------------------------------------

    // Answers one request with a canned response, like LeetCode's endpoint would.
    void serveOnce(QTcpServer &server, const QByteArray &status, const QByteArray &body, QByteArray *request)
    {
        connect(&server, &QTcpServer::newConnection, this, [&server, status, body, request] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, status, body, request] {
                *request += socket->readAll();
                const qsizetype headerEnd = request->indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                static const QRegularExpression lengthHeader("content-length: *(\\d+)", QRegularExpression::CaseInsensitiveOption);
                const qsizetype length = lengthHeader.match(QString::fromLatin1(*request)).captured(1).toLongLong();
                if (request->size() < headerEnd + 4 + length)
                    return;
                socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }

    void fetchesAProblemOverHttp()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QJsonObject question = QJsonDocument::fromJson(readFile(fixturesRoot() + "/problems/two-sum.json")).object();
        const QByteArray body = QJsonDocument(QJsonObject{{"data", QJsonObject{{"question", question}}}}).toJson(QJsonDocument::Compact);
        QByteArray request;
        serveOnce(server, "200 OK", body, &request);

        LeetCodeClient client;
        client.setBaseUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort()));
        QSignalSpy fetched(&client, &LeetCodeClient::problemFetched);
        QSignalSpy failed(&client, &LeetCodeClient::fetchFailed);
        client.fetchProblem("two-sum");
        QTRY_VERIFY_WITH_TIMEOUT(!fetched.isEmpty() || !failed.isEmpty(), 10000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.at(0).at(0).toString()));

        QCOMPARE(fetched.at(0).at(0).value<LeetCodeProblem>().title, QString("Two Sum"));
        QVERIFY(request.startsWith("POST /graphql/"));
        QVERIFY(request.contains("two-sum"));
        QVERIFY(!request.contains("LEETCODE_SESSION"));
    }

    void sendsTheSessionOnlyWhenConnected()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray request;
        serveOnce(server, "200 OK", "{\"data\":{\"question\":null}}", &request);

        LeetCodeClient client;
        client.setBaseUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        client.setSession("secret-session", "token");
        QSignalSpy failed(&client, &LeetCodeClient::fetchFailed);
        client.fetchProblem("nope");
        QVERIFY(failed.wait(10000));
        QVERIFY(failed.at(0).at(0).toString().contains("no problem with the slug"));
        QVERIFY(request.contains("LEETCODE_SESSION=secret-session"));
        QVERIFY(request.toLower().contains("x-csrftoken: token"));
    }

    void explainsHttpFailures()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray request;
        serveOnce(server, "429 Too Many Requests", "{}", &request);

        LeetCodeClient client;
        client.setBaseUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        QSignalSpy failed(&client, &LeetCodeClient::fetchFailed);
        client.fetchProblem("two-sum");
        QVERIFY(failed.wait(10000));
        QVERIFY(failed.at(0).at(0).toString().contains("429"));
    }

    void explainsAnUnreachableServer()
    {
        LeetCodeClient client;
        client.setBaseUrl("http://127.0.0.1:9");
        QSignalSpy failed(&client, &LeetCodeClient::fetchFailed);
        client.fetchProblem("two-sum");
        QVERIFY(failed.wait(10000));
        QVERIFY(failed.at(0).at(0).toString().contains("Could not reach LeetCode"));
        QVERIFY(!client.isBusy());
    }
};

QTEST_GUILESS_MAIN(TestLeetCode)
#include "test_leetcode.moc"
