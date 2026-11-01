#include "debugger/DebuggerManager.h"

#include <QtTest>

#include <csignal>

Q_DECLARE_METATYPE(QList<StackFrame>)
Q_DECLARE_METATYPE(QList<Variable>)

namespace {

constexpr int debugTimeoutMs = 30000;

const char *const sampleSource = R"(#include <iostream>
#include <string>

int square(int value) {
    int result = value * value;
    return result;
}

int main(int argc, char **argv) {
    int number = 7;
    std::string name = "lightide";
    int squared = square(number);
    std::cout << name << " " << squared << std::endl;
    return squared == 49 ? 3 : 0;
}
)";

constexpr int lineInSquare = 5;
constexpr int lineCallingSquare = 12;
constexpr int linePrinting = 13;

struct Session
{
    DebuggerManager debugger;
    QSignalSpy pausedSpy{&debugger, &DebuggerManager::paused};
    QSignalSpy framesSpy{&debugger, &DebuggerManager::framesChanged};
    QSignalSpy variablesSpy{&debugger, &DebuggerManager::variablesChanged};
    QSignalSpy exitedSpy{&debugger, &DebuggerManager::programExited};
    QSignalSpy endedSpy{&debugger, &DebuggerManager::sessionEnded};
    QSignalSpy errorSpy{&debugger, &DebuggerManager::errorOccurred};
    QByteArray programOutput;
    QString console;

    Session()
    {
        QObject::connect(&debugger, &DebuggerManager::inferiorOutput, &debugger,
                         [this](const QByteArray &data) { programOutput += data; });
        QObject::connect(&debugger, &DebuggerManager::consoleOutput, &debugger,
                         [this](const QString &text) { console += text; });
    }

    DebugRequest request(const QString &program, const QList<SourceLocation> &breakpoints = {})
    {
        DebugRequest r;
        r.gdbPath = "gdb";
        r.program = program;
        r.workingDirectory = QFileInfo(program).absolutePath();
        r.breakpoints = breakpoints;
        return r;
    }

    // A stop is only fully reported once the variables for it have arrived.
    bool waitForPause(int expectedPauses)
    {
        QElapsedTimer timer;
        timer.start();
        while (pausedSpy.size() < expectedPauses || variablesSpy.size() < expectedPauses) {
            if (timer.elapsed() > debugTimeoutMs || endedSpy.size() > 0)
                return false;
            QTest::qWait(10);
        }
        return true;
    }

    int currentLine() const { return pausedSpy.last().at(1).toInt(); }
    QString currentFile() const { return pausedSpy.last().at(0).toString(); }

    QList<Variable> variables() const
    {
        return variablesSpy.last().at(0).value<QList<Variable>>();
    }

    QList<StackFrame> frames() const { return framesSpy.last().at(0).value<QList<StackFrame>>(); }

    QString valueOf(const QString &name) const
    {
        const QList<Variable> all = variables();
        for (const Variable &variable : all) {
            if (variable.name == name)
                return variable.value;
        }
        return {};
    }

    bool shutDown()
    {
        if (debugger.isActive())
            debugger.stop();
        if (endedSpy.isEmpty() && !endedSpy.wait(debugTimeoutMs))
            return false;
        return !debugger.isActive();
    }
};

} // namespace

class TestDebugger : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_source;
    QString m_program;

private slots:
    void initTestCase()
    {
        qRegisterMetaType<QList<StackFrame>>();
        qRegisterMetaType<QList<Variable>>();

        QVERIFY(QDir(m_dir.path()).mkpath("my code"));
        m_source = m_dir.filePath("my code/sample app.cpp");
        m_program = m_dir.filePath("my code/sample app");
        QFile file(m_source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(sampleSource);
        file.close();
        QCOMPARE(QProcess::execute("g++", {"-g", "-O0", m_source, "-o", m_program}), 0);
    }

    void stopsAtMainWithoutBreakpoints()
    {
        Session session;
        session.debugger.start(session.request(m_program));
        QVERIFY(session.waitForPause(1));
        QVERIFY2(session.errorSpy.isEmpty(), qPrintable(session.console));
        QCOMPARE(session.debugger.state(), DebuggerManager::State::Paused);
        QCOMPARE(session.frames().at(0).function, QString("main"));
        QVERIFY(session.shutDown());
    }

    void hitsBreakpointInPathWithSpaces()
    {
        Session session;
        session.debugger.start(session.request(m_program, {{m_source, lineInSquare}}));
        QVERIFY(session.waitForPause(1));
        QCOMPARE(session.currentLine(), lineInSquare);
        QCOMPARE(QFileInfo(session.currentFile()).fileName(), QString("sample app.cpp"));
        QVERIFY(session.pausedSpy.last().at(2).toString().contains("Breakpoint"));
        QVERIFY(session.shutDown());
    }

    void showsCallStackAndVariables()
    {
        Session session;
        session.debugger.start(session.request(m_program, {{m_source, lineInSquare}}));
        QVERIFY(session.waitForPause(1));

        const QList<StackFrame> frames = session.frames();
        QVERIFY(frames.size() >= 2);
        QCOMPARE(frames.at(0).function, QString("square"));
        QCOMPARE(frames.at(0).line, lineInSquare);
        QCOMPARE(frames.at(1).function, QString("main"));
        QCOMPARE(frames.at(1).line, lineCallingSquare);

        QCOMPARE(session.valueOf("value"), QString("7"));
        const QList<Variable> variables = session.variables();
        const auto value = std::find_if(variables.cbegin(), variables.cend(),
                                        [](const Variable &v) { return v.name == "value"; });
        QVERIFY(value != variables.cend());
        QVERIFY(value->isArgument);
        QCOMPARE(value->type, QString("int"));
        QVERIFY(session.shutDown());
    }

    void selectingAFrameShowsItsVariables()
    {
        Session session;
        session.debugger.start(session.request(m_program, {{m_source, lineInSquare}}));
        QVERIFY(session.waitForPause(1));

        QSignalSpy selected(&session.debugger, &DebuggerManager::frameSelected);
        const int before = session.variablesSpy.size();
        session.debugger.selectFrame(1);
        QTRY_VERIFY_WITH_TIMEOUT(session.variablesSpy.size() > before, 5000);
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.at(0).at(1).toInt(), lineCallingSquare);
        QCOMPARE(session.valueOf("number"), QString("7"));
        QVERIFY(session.valueOf("name").contains("lightide"));
        QVERIFY(session.shutDown());
    }

    void stepsOverIntoAndOut()
    {
        Session session;
        session.debugger.start(session.request(m_program, {{m_source, lineCallingSquare}}));
        QVERIFY(session.waitForPause(1));
        QCOMPARE(session.currentLine(), lineCallingSquare);

        session.debugger.stepInto();
        QVERIFY(session.waitForPause(2));
        QCOMPARE(session.frames().at(0).function, QString("square"));

        session.debugger.stepOut();
        QVERIFY(session.waitForPause(3));
        QCOMPARE(session.frames().at(0).function, QString("main"));

        session.debugger.stepOver();
        QVERIFY(session.waitForPause(4));
        QCOMPARE(session.currentLine(), linePrinting);
        QCOMPARE(session.valueOf("squared"), QString("49"));

        QVERIFY(session.shutDown());
    }

    void stepIntoSkipsLibraryCode()
    {
        Session session;
        session.debugger.start(session.request(m_program, {{m_source, linePrinting}}));
        QVERIFY(session.waitForPause(1));
        session.debugger.stepInto();
        QVERIFY(session.waitForPause(2));
        // Without the skip rules this would land inside libstdc++'s headers.
        QCOMPARE(session.frames().at(0).function, QString("main"));
        QVERIFY(session.shutDown());
    }

    void continueRunsToExitAndReportsCode()
    {
        Session session;
        session.debugger.start(session.request(m_program, {{m_source, lineInSquare}}));
        QVERIFY(session.waitForPause(1));

        session.debugger.continueExecution();
        QVERIFY(session.exitedSpy.wait(debugTimeoutMs));
        QCOMPARE(session.exitedSpy.at(0).at(0).toInt(), 3);
        QVERIFY(session.endedSpy.size() > 0 || session.endedSpy.wait(debugTimeoutMs));
        QCOMPARE(session.debugger.state(), DebuggerManager::State::Idle);
        QVERIFY(session.programOutput.contains("lightide 49"));
    }

    void breakpointsCanBeAddedAndRemovedWhilePaused()
    {
        Session session;
        session.debugger.start(session.request(m_program, {{m_source, lineCallingSquare}}));
        QVERIFY(session.waitForPause(1));

        session.debugger.addBreakpoint({m_source, lineInSquare});
        session.debugger.continueExecution();
        QVERIFY(session.waitForPause(2));
        QCOMPARE(session.currentLine(), lineInSquare);

        session.debugger.removeBreakpoint({m_source, lineInSquare});
        session.debugger.continueExecution();
        QVERIFY(session.exitedSpy.wait(debugTimeoutMs));
        QVERIFY(session.shutDown());
    }

    void evaluatesExpressions()
    {
        Session session;
        session.debugger.start(session.request(m_program, {{m_source, lineInSquare}}));
        QVERIFY(session.waitForPause(1));
        session.debugger.evaluate("value * 2");
        QTRY_VERIFY_WITH_TIMEOUT(session.console.contains("value * 2 = 14"), 5000);
        QVERIFY(session.shutDown());
    }

    void stopWhileProgramIsRunning()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("spin.cpp");
        const QString program = dir.filePath("spin");
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("#include <cstdio>\n"
                       "int main() {\n"
                       "    puts(\"spinning\");\n"
                       "    fflush(stdout);\n"
                       "    for (;;) {}\n"
                       "}\n");
        }
        QCOMPARE(QProcess::execute("g++", {"-g", source, "-o", program}), 0);

        Session session;
        session.debugger.start(session.request(program, {{source, 3}}));
        QVERIFY(session.waitForPause(1));
        session.debugger.continueExecution();
        QTRY_VERIFY_WITH_TIMEOUT(session.programOutput.contains("spinning"), debugTimeoutMs);
        QCOMPARE(session.debugger.state(), DebuggerManager::State::Running);

        session.debugger.stop();
        QVERIFY(session.endedSpy.wait(debugTimeoutMs));
        QCOMPARE(session.debugger.state(), DebuggerManager::State::Idle);
    }

    void pausesOnCrash()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("crash.cpp");
        const QString program = dir.filePath("crash");
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("int main() {\n"
                       "    volatile int *p = nullptr;\n"
                       "    *p = 1;\n"
                       "    return 0;\n"
                       "}\n");
        }
        QCOMPARE(QProcess::execute("g++", {"-g", source, "-o", program}), 0);

        Session session;
        session.debugger.start(session.request(program, {{source, 2}}));
        QVERIFY(session.waitForPause(1));
        session.debugger.continueExecution();
        QVERIFY(session.waitForPause(2));
        QCOMPARE(session.currentLine(), 3);
        QVERIFY(session.pausedSpy.last().at(2).toString().contains("SIGSEGV"));
        QVERIFY(session.shutDown());
    }

    void programInputReachesTheInferior()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("echo.cpp");
        const QString program = dir.filePath("echo");
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("#include <iostream>\n"
                       "#include <string>\n"
                       "int main() {\n"
                       "    std::string s;\n"
                       "    std::cin >> s;\n"
                       "    std::cout << \"got \" << s << std::endl;\n"
                       "}\n");
        }
        QCOMPARE(QProcess::execute("g++", {"-g", source, "-o", program}), 0);

        Session session;
        session.debugger.start(session.request(program, {{source, 4}}));
        QVERIFY(session.waitForPause(1));
        session.debugger.continueExecution();
        QTRY_COMPARE_WITH_TIMEOUT(session.debugger.state(), DebuggerManager::State::Running, 5000);
        session.debugger.writeToProgram("hello\n");
        QVERIFY(session.exitedSpy.wait(debugTimeoutMs));
        QVERIFY(session.programOutput.contains("got hello"));
        QVERIFY(session.shutDown());
    }

    void passesProgramArguments()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("args.cpp");
        const QString program = dir.filePath("args");
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("#include <cstdio>\n"
                       "int main(int argc, char **argv) {\n"
                       "    printf(\"argc=%d [%s]\\n\", argc, argv[1]);\n"
                       "    return 0;\n"
                       "}\n");
        }
        QCOMPARE(QProcess::execute("g++", {"-g", source, "-o", program}), 0);

        Session session;
        DebugRequest request = session.request(program, {{source, 3}});
        request.arguments = "\"two words\"";
        session.debugger.start(request);
        QVERIFY(session.waitForPause(1));
        session.debugger.continueExecution();
        QVERIFY(session.exitedSpy.wait(debugTimeoutMs));
        QVERIFY(session.programOutput.contains("argc=2 [two words]"));
        QVERIFY(session.shutDown());
    }

    void destroyingAPausedSessionEndsTheProgram()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("self.cpp");
        const QString program = dir.filePath("self");
        const QString pidFile = dir.filePath("pid");
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("#include <cstdio>\n"
                       "#include <unistd.h>\n"
                       "int main() {\n"
                       "    FILE *f = fopen(\"" + pidFile.toUtf8() + "\", \"w\");\n"
                       "    fprintf(f, \"%d\", getpid());\n"
                       "    fclose(f);\n"
                       "    return 0;\n"
                       "}\n");
        }
        QCOMPARE(QProcess::execute("g++", {"-g", source, "-o", program}), 0);

        pid_t inferior = 0;
        {
            Session session;
            session.debugger.start(session.request(program, {{source, 7}}));
            QVERIFY(session.waitForPause(1));
            QFile file(pidFile);
            QVERIFY(file.open(QIODevice::ReadOnly));
            inferior = file.readAll().toInt();
            QVERIFY(inferior > 0);
            QCOMPARE(::kill(inferior, 0), 0);
        }
        QTRY_VERIFY_WITH_TIMEOUT(::kill(inferior, 0) != 0, 3000);
    }

    void aSecondStepWhileSteppingIsIgnored()
    {
        Session session;
        session.debugger.start(session.request(m_program, {{m_source, lineCallingSquare}}));
        QVERIFY(session.waitForPause(1));

        session.debugger.stepOver();
        session.debugger.stepOver();
        session.debugger.stepOver();
        QVERIFY(session.waitForPause(2));
        QTest::qWait(100);
        QCOMPARE(session.pausedSpy.size(), 2);
        QCOMPARE(session.currentLine(), linePrinting);
        QVERIFY(!session.console.contains("running"));
        QVERIFY(session.shutDown());
    }

    void stepOutOfMainReportsAndStaysPaused()
    {
        Session session;
        session.debugger.start(session.request(m_program));
        QVERIFY(session.waitForPause(1));
        session.debugger.stepOut();
        QTRY_VERIFY_WITH_TIMEOUT(session.console.contains("outermost"), 5000);
        QCOMPARE(session.debugger.state(), DebuggerManager::State::Paused);
        QVERIFY(session.shutDown());
    }

    void reportsBinaryThatCannotBeLoaded()
    {
        Session session;
        session.debugger.start(session.request(m_dir.filePath("does-not-exist")));
        QVERIFY(session.endedSpy.wait(debugTimeoutMs));
        QVERIFY(!session.errorSpy.isEmpty());
        QCOMPARE(session.debugger.state(), DebuggerManager::State::Idle);
    }

    void reportsMissingGdb()
    {
        Session session;
        DebugRequest request = session.request(m_program);
        request.gdbPath = "/nonexistent/gdb";
        session.debugger.start(request);
        QTRY_VERIFY_WITH_TIMEOUT(!session.errorSpy.isEmpty(), 5000);
        QCOMPARE(session.debugger.state(), DebuggerManager::State::Idle);
    }
};

QTEST_GUILESS_MAIN(TestDebugger)
#include "test_debugger.moc"
