#include "compiler/BuildProcess.h"
#include "compiler/CompilerManager.h"
#include "compiler/DiagnosticsParser.h"
#include "core/Settings.h"

#include <QtTest>

Q_DECLARE_METATYPE(BuildResult)
Q_DECLARE_METATYPE(Diagnostic)

namespace {

void writeText(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

BuildPlan singleFilePlan(const QString &source, const Toolchain &tools = {})
{
    const QString output = CompileCommand::outputPathFor(source, {});
    BuildPlan plan;
    plan.compileJobs.append(BuildJob{
        CompileCommand::singleFile(tools, BuildOptions{}, source, output), source});
    plan.workingDirectory = QFileInfo(source).absolutePath();
    plan.outputPath = output;
    return plan;
}

struct Outcome
{
    BuildResult result;
    QList<Diagnostic> diagnostics;
    QStringList commands;
    QStringList lines;
};

Outcome run(const BuildPlan &plan)
{
    Outcome outcome;
    BuildProcess build;
    QObject::connect(&build, &BuildProcess::diagnosticFound, &build,
                     [&](const Diagnostic &d) { outcome.diagnostics.append(d); });
    QObject::connect(&build, &BuildProcess::commandStarted, &build,
                     [&](const QString &c) { outcome.commands.append(c); });
    QObject::connect(&build, &BuildProcess::outputLine, &build,
                     [&](const QString &l) { outcome.lines.append(l); });
    QSignalSpy finished(&build, &BuildProcess::finished);
    build.start(plan);
    if (finished.isEmpty())
        finished.wait(60000);
    outcome.result = finished.at(0).at(0).value<BuildResult>();
    return outcome;
}

} // namespace

class TestBuild : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<BuildResult>();
        qRegisterMetaType<Diagnostic>();
    }

    void compilesHelloWorld()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("hello.cpp");
        writeText(source, "#include <iostream>\nint main() { std::cout << \"hi\\n\"; }\n");

        const Outcome outcome = run(singleFilePlan(source));
        QVERIFY(outcome.result.success);
        QCOMPARE(outcome.result.exitCode, 0);
        QCOMPARE(outcome.result.errors, 0);
        QCOMPARE(outcome.commands.size(), 1);
        QVERIFY(outcome.commands.at(0).contains("hello.cpp"));
        QVERIFY(QFileInfo(outcome.result.outputPath).isExecutable());
    }

    void compilesCProgram()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("hello.c");
        writeText(source, "#include <stdio.h>\nint main(void) { puts(\"hi\"); return 0; }\n");

        const Outcome outcome = run(singleFilePlan(source));
        QVERIFY(outcome.result.success);
        QVERIFY(outcome.commands.at(0).startsWith("gcc"));
    }

    void reportsErrorsInPathWithSpaces()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("my code");
        const QString source = dir.filePath("my code/bad file.cpp");
        writeText(source, "int main() {\n    int x = y;\n}\n");

        const Outcome outcome = run(singleFilePlan(source));
        QVERIFY(!outcome.result.success);
        QVERIFY(outcome.result.exitCode != 0);
        QVERIFY(outcome.result.errors >= 1);
        QVERIFY(!outcome.diagnostics.isEmpty());

        const Diagnostic &first = outcome.diagnostics.first();
        QCOMPARE(first.severity, Diagnostic::Severity::Error);
        QCOMPARE(first.file, source);
        QCOMPARE(first.line, 2);
        QCOMPARE(first.column, 13);
    }

    void countsWarningsWithoutFailing()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("warn.cpp");
        writeText(source, "int main() {\n    int unused;\n    return 0;\n}\n");

        const Outcome outcome = run(singleFilePlan(source));
        QVERIFY(outcome.result.success);
        QCOMPARE(outcome.result.errors, 0);
        QCOMPARE(outcome.result.warnings, 1);
        QCOMPARE(outcome.diagnostics.first().severity, Diagnostic::Severity::Warning);
    }

    void reportsLinkerFailures()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("link.cpp");
        writeText(source, "void missing();\nint main() {\n    missing();\n}\n");

        const Outcome outcome = run(singleFilePlan(source));
        QVERIFY(!outcome.result.success);
        const auto undefinedReference = std::find_if(
            outcome.diagnostics.cbegin(), outcome.diagnostics.cend(), [](const Diagnostic &d) {
                return d.message.contains("undefined reference");
            });
        QVERIFY(undefinedReference != outcome.diagnostics.cend());
        QCOMPARE(undefinedReference->file, source);
        QCOMPARE(undefinedReference->line, 3);
    }

    void missingCompilerFailsCleanly()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("a.cpp");
        writeText(source, "int main() {}\n");
        const Toolchain tools{"/nonexistent/gcc", "/nonexistent/g++"};

        const Outcome outcome = run(singleFilePlan(source, tools));
        QVERIFY(!outcome.result.success);
        QVERIFY(outcome.result.failureMessage.contains("/nonexistent/g++"));
    }

    void cancelStopsARunningCompile()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("slow.cpp");
        // Instantiation depth that keeps cc1plus busy for many seconds.
        writeText(source,
                  "#include <regex>\n#include <iostream>\n"
                  "template<int N> struct S { static const int v = S<N-1>::v + S<N-2>::v; };\n"
                  "template<> struct S<0> { static const int v = 0; };\n"
                  "template<> struct S<1> { static const int v = 1; };\n"
                  "int main() { std::cout << S<20>::v; std::regex r(\"a\"); }\n");

        BuildProcess build;
        QSignalSpy finished(&build, &BuildProcess::finished);
        QElapsedTimer timer;
        timer.start();
        build.start(singleFilePlan(source));
        QVERIFY(build.isRunning());
        QTimer::singleShot(100, &build, &BuildProcess::cancel);
        QVERIFY(finished.wait(10000));
        const auto result = finished.at(0).at(0).value<BuildResult>();
        QVERIFY(result.cancelled);
        QVERIFY(!result.success);
        QVERIFY(timer.elapsed() < 5000);
    }

    void stopsAfterFirstFailedCompileAndSkipsLink()
    {
        QTemporaryDir dir;
        writeText(dir.filePath("bad.cpp"), "this does not compile\n");
        BuildPlan plan;
        plan.workingDirectory = dir.path();
        plan.compileJobs.append(BuildJob{
            CompileCommand::object({}, {}, dir.filePath("bad.cpp"), dir.filePath("bad.o")), "bad"});
        plan.linkJob = BuildJob{CommandLine{"g++", {dir.filePath("bad.o")}}, "link"};

        const Outcome outcome = run(plan);
        QVERIFY(!outcome.result.success);
        QCOMPARE(outcome.commands.size(), 1);
    }

    void detectsInstalledTools()
    {
        CompilerManager manager;
        QSignalSpy finished(&manager, &CompilerManager::detectionFinished);
        manager.detect(CompilerSettings{});
        QVERIFY(finished.wait(10000));
        QVERIFY(manager.gcc().found);
        QVERIFY(manager.gxx().found);
        QVERIFY(manager.gdb().found);
        QVERIFY(!manager.gxx().version.isEmpty());
        QVERIFY(manager.gxx().path.startsWith('/'));
    }

    void reportsMissingTools()
    {
        CompilerSettings settings;
        settings.gxxPath = "/nonexistent/g++";
        settings.gdbPath = "definitely-not-installed-gdb";

        CompilerManager manager;
        QSignalSpy finished(&manager, &CompilerManager::detectionFinished);
        manager.detect(settings);
        QVERIFY(finished.wait(10000));
        QVERIFY(!manager.gxx().found);
        QVERIFY(!manager.gdb().found);
        QVERIFY(manager.gcc().found);
        QVERIFY(CompilerManager::installHint().contains("build-essential gdb"));
    }
};

QTEST_GUILESS_MAIN(TestBuild)
#include "test_build.moc"
