#include "execution/ProcessRunner.h"

#include <QtTest>

#include <csignal>

Q_DECLARE_METATYPE(RunResult)

namespace {

constexpr int runTimeoutMs = 15000;

// Compiles a test program; the C driver is used so that each build stays fast.
QString compile(const QTemporaryDir &dir, const QString &name, const QByteArray &source)
{
    const QString sourcePath = dir.filePath(name + ".c");
    {
        QFile file(sourcePath);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write(source);
    }
    const QString output = dir.filePath(name);
    if (QProcess::execute("gcc", {"-w", sourcePath, "-o", output}) != 0)
        return {};
    return output;
}

// A killed process stays a zombie until its parent reaps it, and an orphan's new
// parent may never do so: in a container, PID 1 is often not a real init. A
// zombie runs nothing, so kill(pid, 0) alone would call it alive.
bool processIsRunning(pid_t pid)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return false;
    const QByteArray line = stat.readAll();
    const qsizetype state = line.lastIndexOf(')') + 2;
    return state < line.size() && line.at(state) != 'Z' && line.at(state) != 'X';
}

struct Session
{
    ProcessRunner runner;
    QByteArray output;
    QSignalSpy finished{&runner, &ProcessRunner::finished};

    Session()
    {
        QObject::connect(&runner, &ProcessRunner::output, &runner,
                         [this](const QByteArray &data) { output += data; });
    }

    RunResult result() const { return finished.at(0).at(0).value<RunResult>(); }

    bool waitForOutput(const QByteArray &text, int timeoutMs = runTimeoutMs)
    {
        QElapsedTimer timer;
        timer.start();
        while (!output.contains(text)) {
            if (timer.elapsed() > timeoutMs)
                return false;
            QTest::qWait(5);
        }
        return true;
    }
};

} // namespace

class TestRunner : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<RunResult>(); }

    void capturesStdoutAndExitCode()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "hello", R"(#include <stdio.h>
            int main() { puts("hello"); return 0; })");
        QVERIFY(!program.isEmpty());

        Session session;
        QString error;
        QVERIFY2(session.runner.start({program, {}, {}}, &error), qPrintable(error));
        QVERIFY(session.runner.isRunning());
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(session.output.contains("hello"));
        QCOMPARE(session.result().exitCode, 0);
        QCOMPARE(session.result().signal, 0);
        QVERIFY(!session.runner.isRunning());
    }

    void capturesStderr()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "err", R"(#include <stdio.h>
            int main() { fprintf(stderr, "to stderr\n"); return 0; })");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(session.output.contains("to stderr"));
    }

    void reportsNonZeroExit()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "fail", "int main() { return 42; }");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QCOMPARE(session.result().exitCode, 42);
        QVERIFY(session.result().description().contains("code 42"));
    }

    void promptAppearsBeforeInputIsGiven()
    {
        // Over pipes this printf stays in stdio's buffer until exit, so the
        // prompt would never show. This is the reason for using a terminal.
        QTemporaryDir dir;
        const QString program = compile(dir, "prompt", R"(#include <stdio.h>
            int main() {
                char name[64];
                printf("Name: ");
                if (scanf("%63s", name) != 1) return 1;
                printf("Hello, %s!\n", name);
                return 0;
            })");

        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.waitForOutput("Name: "));
        QVERIFY(session.runner.isRunning());

        session.runner.write("Ammar\n");
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(session.output.contains("Hello, Ammar!"));
        QCOMPARE(session.result().exitCode, 0);
    }

    void cinWithMultipleReads()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("sum.cpp");
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(R"(#include <iostream>
                int main() {
                    int a, b;
                    std::cout << "a? ";
                    std::cin >> a;
                    std::cout << "b? ";
                    std::cin >> b;
                    std::cout << "sum=" << a + b << std::endl;
                })");
        }
        const QString program = dir.filePath("sum");
        QCOMPARE(QProcess::execute("g++", {source, "-o", program}), 0);

        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.waitForOutput("a? "));
        session.runner.write("20\n");
        QVERIFY(session.waitForOutput("b? "));
        session.runner.write("22\n");
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(session.output.contains("sum=42"));
    }

    void endOfInputWithCtrlD()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "eof", R"(#include <stdio.h>
            int main() {
                int c, n = 0;
                while ((c = getchar()) != EOF) n++;
                printf("read %d\n", n);
                return 0;
            })");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        session.runner.write("abc\n");
        session.runner.write(QByteArray(1, '\x04'));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(session.output.contains("read 4"));
    }

    void ctrlCInterruptsTheProgram()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "loop", R"(#include <stdio.h>
            int main() { puts("started"); fflush(stdout); for (;;) {} })");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.waitForOutput("started"));
        session.runner.write(QByteArray(1, '\x03'));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QCOMPARE(session.result().signal, SIGINT);
    }

    void passesArgumentsWithSpaces()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "args", R"(#include <stdio.h>
            int main(int argc, char **argv) {
                printf("argc=%d\n", argc);
                for (int i = 1; i < argc; i++) printf("[%s]\n", argv[i]);
                return 0;
            })");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {"one", "two words"}, {}}, &error));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(session.output.contains("argc=3"));
        QVERIFY(session.output.contains("[two words]"));
    }

    void usesWorkingDirectory()
    {
        QTemporaryDir dir;
        QTemporaryDir other;
        const QString program = compile(dir, "cwd", R"(#include <stdio.h>
            #include <unistd.h>
            int main() { char b[4096]; puts(getcwd(b, sizeof b)); return 0; })");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, other.path()}, &error));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(session.output.contains(QFileInfo(other.path()).canonicalFilePath().toUtf8()));
    }

    void runsFromPathWithSpaces()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("my dir");
        const QString source = dir.filePath("my dir/a b.c");
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("#include <stdio.h>\nint main() { puts(\"spaced\"); return 0; }\n");
        }
        const QString program = dir.filePath("my dir/a b");
        QCOMPARE(QProcess::execute("gcc", {source, "-o", program}), 0);

        Session session;
        QString error;
        QVERIFY2(session.runner.start({program, {}, {}}, &error), qPrintable(error));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(session.output.contains("spaced"));
    }

    void stopEndsAnInfiniteLoop()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "spin", R"(#include <stdio.h>
            int main() { puts("spinning"); fflush(stdout); for (;;) {} })");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.waitForOutput("spinning"));

        QElapsedTimer timer;
        timer.start();
        session.runner.stop();
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY(session.result().stoppedByUser);
        QVERIFY(!session.result().crashed());
        QVERIFY(session.result().description().contains("Stopped"));
    }

    void stopEscalatesWhenSigtermIsIgnored()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "stubborn", R"(#include <signal.h>
            #include <stdio.h>
            int main() {
                signal(SIGTERM, SIG_IGN);
                puts("armed");
                fflush(stdout);
                for (;;) {}
            })");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.waitForOutput("armed"));
        session.runner.stop();
        QVERIFY(session.finished.wait(runTimeoutMs));
        QCOMPARE(session.result().signal, SIGKILL);
    }

    void stopAlsoEndsChildProcesses()
    {
        QTemporaryDir dir;
        const QString pidFile = dir.filePath("child.pid");
        const QByteArray source = "#include <stdio.h>\n"
                                  "#include <unistd.h>\n"
                                  "int main() {\n"
                                  "    pid_t child = fork();\n"
                                  "    if (child == 0) { for (;;) pause(); }\n"
                                  "    FILE *f = fopen(\"" + pidFile.toUtf8() + "\", \"w\");\n"
                                  "    fprintf(f, \"%d\", child);\n"
                                  "    fclose(f);\n"
                                  "    puts(\"forked\");\n"
                                  "    fflush(stdout);\n"
                                  "    for (;;) pause();\n"
                                  "}\n";
        const QString program = compile(dir, "parent", source);
        QVERIFY(!program.isEmpty());

        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.waitForOutput("forked"));

        QFile file(pidFile);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const pid_t child = file.readAll().toInt();
        QVERIFY(child > 0);
        QVERIFY(processIsRunning(child));

        session.runner.stop();
        QVERIFY(session.finished.wait(runTimeoutMs));
        QTRY_VERIFY_WITH_TIMEOUT(!processIsRunning(child), 3000);
    }

    void reportsSegfault()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "crash", "int main() { *(volatile int *)0 = 1; }");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QCOMPARE(session.result().signal, SIGSEGV);
        QVERIFY(session.result().crashed());
        QVERIFY(session.result().description().contains("Segmentation fault"));
    }

    void outputFloodDoesNotBlockTheEventLoop()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "flood", R"(#include <stdio.h>
            int main() { for (;;) puts("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"); })");

        ProcessRunner runner;
        qint64 received = 0;
        connect(&runner, &ProcessRunner::output, this,
                [&](const QByteArray &data) { received += data.size(); });
        QSignalSpy finished(&runner, &ProcessRunner::finished);

        // Timer ticks measure how long the loop can be held up.
        int ticks = 0;
        QTimer ticker;
        connect(&ticker, &QTimer::timeout, this, [&] { ++ticks; });
        ticker.start(10);

        QString error;
        QVERIFY(runner.start({program, {}, {}}, &error));
        QTest::qWait(500);
        QVERIFY2(ticks >= 25, qPrintable(QString("only %1 ticks").arg(ticks)));
        QVERIFY(received > 0);

        runner.stop();
        QVERIFY(finished.wait(runTimeoutMs));
    }

    void finalOutputIsNotLostWhenTheProgramExits()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "burst", R"(#include <stdio.h>
            int main() {
                for (int i = 0; i < 2000; i++) printf("line %d\n", i);
                printf("the end\n");
                return 0;
            })");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY(session.output.contains("line 1999"));
        QVERIFY(session.output.contains("the end"));
    }

    void rejectsMissingProgram()
    {
        ProcessRunner runner;
        QString error;
        QVERIFY(!runner.start({"/nonexistent/program", {}, {}}, &error));
        QVERIFY(error.contains("/nonexistent/program"));
        QVERIFY(!runner.isRunning());
    }

    void rejectsMissingWorkingDirectory()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "ok", "int main() { return 0; }");
        ProcessRunner runner;
        QString error;
        QVERIFY(!runner.start({program, {}, "/nonexistent/dir"}, &error));
        QVERIFY(error.contains("/nonexistent/dir"));
    }

    void reportsExecFailure()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("not-a-program");
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("plain text, not an executable format\n");
        }
        QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner);

        ProcessRunner runner;
        QString error;
        QVERIFY(!runner.start({path, {}, {}}, &error));
        QVERIFY(!runner.isRunning());
        QVERIFY(!error.isEmpty());
    }

    void destructorLeavesNoProcessBehind()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "hang", "int main() { for (;;) {} }");
        qint64 pid = 0;
        {
            ProcessRunner runner;
            QString error;
            QVERIFY(runner.start({program, {}, {}}, &error));
            pid = runner.pid();
            QCOMPARE(::kill(static_cast<pid_t>(pid), 0), 0);
        }
        QCOMPARE(::kill(static_cast<pid_t>(pid), 0), -1);
    }

    void restartsAfterFinishing()
    {
        QTemporaryDir dir;
        const QString program = compile(dir, "again", R"(#include <stdio.h>
            int main() { puts("run"); return 0; })");
        Session session;
        QString error;
        QVERIFY(session.runner.start({program, {}, {}}, &error));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QVERIFY2(session.runner.start({program, {}, {}}, &error), qPrintable(error));
        QVERIFY(session.finished.wait(runTimeoutMs));
        QCOMPARE(session.finished.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestRunner)
#include "test_runner.moc"
