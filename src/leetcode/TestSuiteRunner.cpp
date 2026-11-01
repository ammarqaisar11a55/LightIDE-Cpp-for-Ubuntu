#include "leetcode/TestSuiteRunner.h"

#include "leetcode/OutputComparator.h"
#include "leetcode/QuestionParser.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>

#include <algorithm>

namespace {

constexpr qsizetype maxOutputBytes = 4 * 1024 * 1024;

QString newNonce()
{
    return QString::number(QRandomGenerator::global()->generate64(), 16);
}

QString trimEnd(const QString &text)
{
    qsizetype end = text.size();
    while (end > 0 && text.at(end - 1).isSpace())
        --end;
    return text.left(end);
}

} // namespace

TestSuiteRunner::TestSuiteRunner(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        if (m_process)
            m_process->kill();
    });
}

TestSuiteRunner::~TestSuiteRunner()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(500);
    }
}

void TestSuiteRunner::start(const ProblemFolder &folder, const ProblemMetadata &metadata,
                            const QList<LcTestCase> &tests, const LeetCodeRunSettings &settings,
                            const QList<int> &onlyTests)
{
    if (isRunning())
        return;

    m_folder = folder;
    m_metadata = metadata;
    m_tests = tests;
    m_settings = settings;
    m_only = onlyTests;
    m_report = LcRunReport{};
    m_queue.clear();
    m_cancelled = false;
    m_phase = Phase::Listing;
    m_nonce = newNonce();
    launch({QStringLiteral("--lc-list"), QStringLiteral("--lc-nonce"), m_nonce});
}

void TestSuiteRunner::cancel()
{
    m_cancelled = true;
    m_queue.clear();
    if (m_process)
        m_process->kill();
}

void TestSuiteRunner::launch(const QStringList &arguments)
{
    m_output.clear();
    m_errors.clear();
    m_timedOut = false;
    m_overLimit = false;

    auto *process = new QProcess(this);
    process->setWorkingDirectory(m_folder.directory());

    // Test inputs are deliberately never freed, so leak reports would only be noise.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (!environment.contains(QStringLiteral("ASAN_OPTIONS")))
        environment.insert(QStringLiteral("ASAN_OPTIONS"), QStringLiteral("detect_leaks=0"));
    process->setProcessEnvironment(environment);

    const auto collect = [this, process](QByteArray &buffer, QByteArray data) {
        buffer += data;
        if (m_output.size() + m_errors.size() > maxOutputBytes && !m_overLimit) {
            m_overLimit = true;
            process->kill();
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, collect] {
        collect(m_output, process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process, collect] {
        collect(m_errors, process->readAllStandardError());
    });
    connect(process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                const bool crashed = status == QProcess::CrashExit;
                onProcessFinished(crashed ? 0 : exitCode, crashed ? exitCode : 0, crashed);
            });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            onProcessFinished(-1, 0, false);
    });

    m_process = process;
    m_timeout.start(m_settings.timeoutMs);
    process->start(m_folder.binaryPath(), arguments);
}

void TestSuiteRunner::onProcessFinished(int exitCode, int signal, bool crashed)
{
    Q_UNUSED(crashed)
    if (!m_process)
        return;

    m_timeout.stop();
    m_output += m_process->readAllStandardOutput();
    m_errors += m_process->readAllStandardError();
    m_process->deleteLater();
    m_process = nullptr;

    const QString output = QString::fromUtf8(m_output);
    const QString errors = QString::fromUtf8(m_errors);

    if (m_cancelled) {
        complete();
    } else if (m_phase == Phase::Listing) {
        onListed(output, exitCode, signal, errors);
    } else {
        const LcTestResult result = evaluate(m_currentId, output, errors, exitCode, signal);
        m_report.tests.append(result);
        emit testFinished(result);
        runNext();
    }
}

void TestSuiteRunner::onListed(const QString &output, int exitCode, int signal, const QString &errors)
{
    const ParsedHarnessOutput parsed = OutputComparator::parseHarnessOutput(output, m_nonce);
    QList<int> ids;
    for (const HarnessRecord &record : parsed.records) {
        if (record.event == u"test")
            ids.append(record.id);
    }

    if (ids.isEmpty() && (exitCode != 0 || signal != 0 || m_timedOut)) {
        const QString reason = m_timedOut ? QStringLiteral("timed out")
                                          : OutputComparator::describeCrash(exitCode, signal);
        m_report.error = trimEnd(QStringLiteral("The test program failed before running any test: %1\n%2")
                                     .arg(reason, errors));
        complete();
        return;
    }

    for (const int id : std::as_const(ids)) {
        if (m_only.isEmpty() || m_only.contains(id))
            m_queue.append(id);
    }
    m_phase = Phase::Running;
    runNext();
}

void TestSuiteRunner::runNext()
{
    if (m_queue.isEmpty()) {
        complete();
        return;
    }
    m_currentId = m_queue.takeFirst();
    m_nonce = newNonce();
    launch({QStringLiteral("--lc-test"), QString::number(m_currentId), QStringLiteral("--lc-nonce"), m_nonce});
}

LcTestResult TestSuiteRunner::evaluate(int id, const QString &output, const QString &errors,
                                       int exitCode, int signal)
{
    const ParsedHarnessOutput parsed = OutputComparator::parseHarnessOutput(output, m_nonce);
    const auto record = std::find_if(parsed.records.cbegin(), parsed.records.cend(), [id](const HarnessRecord &r) {
        return r.event == u"result" && r.id == id;
    });
    const auto test = std::find_if(m_tests.cbegin(), m_tests.cend(),
                                   [id](const LcTestCase &t) { return t.id == id; });
    const bool hasRecord = record != parsed.records.cend();
    const bool hasTest = test != m_tests.cend();

    LcTestResult result;
    result.id = id;
    result.name = hasTest ? test->name : QStringLiteral("Test %1").arg(id);
    result.input = hasTest ? HarnessGenerator::describeInputs(QuestionParser::inputKeys(m_metadata.meta), test->inputs)
                           : QStringLiteral("(defined in the manual harness)");
    result.stdoutText = trimEnd(parsed.userStdout);
    result.stderrText = trimEnd(errors);

    // The harness's copy wins, because a manual harness may differ from tests.json.
    std::optional<LcValue> expectedValue;
    if (hasRecord && record->expected) {
        result.expected = record->expected;
        try {
            expectedValue = QuestionParser::parseOutputText(*record->expected);
        } catch (const LcError &) {
            // Shown for manual comparison.
        }
    } else if (hasTest && test->expected) {
        expectedValue = test->expected;
        result.expected = toLeetCodeText(*test->expected);
    }

    if (m_timedOut) {
        result.status = LcTestStatus::Timeout;
        result.message = QStringLiteral("Execution timed out after %1 ms.\nPossible infinite loop or "
                                        "extremely slow solution.").arg(m_settings.timeoutMs);
        return result;
    }
    if (m_overLimit) {
        result.status = LcTestStatus::OutputLimit;
        result.message = QStringLiteral("The program printed too much output and was stopped (possible "
                                        "infinite loop around a print statement).");
        return result;
    }
    if (!hasRecord) {
        result.message = OutputComparator::describeCrash(exitCode, signal);
        return result;
    }

    if (record->timeUs)
        result.timeMs = *record->timeUs / 1000.0;
    if (record->status != u"ok") {
        result.message = record->status == u"not_wired"
                             ? record->error
                             : QStringLiteral("Uncaught exception: %1")
                                   .arg(record->error.isEmpty() ? QStringLiteral("unknown") : record->error);
        return result;
    }

    result.actual = record->output.value_or(QString());
    if (!expectedValue) {
        result.status = LcTestStatus::NoExpected;
        if (result.expected)
            result.hint = QStringLiteral("The expected output could not be parsed, so it is shown for manual comparison.");
        return result;
    }

    try {
        const LcValue actual = QuestionParser::parseOutputText(*result.actual);
        const ComparisonResult comparison = OutputComparator::compare(actual, *expectedValue, m_metadata.comparison);
        result.status = comparison.equal ? LcTestStatus::Pass : LcTestStatus::Fail;
        result.hint = comparison.hint;
    } catch (const LcError &error) {
        result.status = LcTestStatus::Fail;
        result.message = QStringLiteral("Could not parse the output: %1").arg(error.message());
    }
    return result;
}

void TestSuiteRunner::complete()
{
    m_report.total = static_cast<int>(m_report.tests.size());
    for (const LcTestResult &result : std::as_const(m_report.tests)) {
        if (result.status == LcTestStatus::Pass)
            ++m_report.passed;
        else if (result.status != LcTestStatus::NoExpected)
            m_report.failed = true;
        m_report.totalTimeMs += result.timeMs.value_or(0);
    }
    emit finished(m_report);
}
