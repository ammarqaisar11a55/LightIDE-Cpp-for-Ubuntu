// End-to-end tests. Each one drives the real MainWindow through its actions,
// keystrokes and dialogs, against the real compiler, debugger and processes.
// Set LIGHTIDE_SCREENSHOT_DIR to save screenshots of the key states.

#include "compiler/CompilerManager.h"
#include "core/Settings.h"
#include "editor/CodeEditor.h"
#include "project/Project.h"
#include <Qsci/qscilexer.h>
#include "ui/DebugPanel.h"
#include "ui/EditorWidget.h"
#include "ui/FindBar.h"
#include "ui/FindInFilesDialog.h"
#include "ui/OutputPanel.h"
#include "ui/SettingsDialog.h"
#include "ui/Sidebar.h"
#include "ui/TerminalView.h"
#include "ui/MainWindow.h"

#include <QtTest>
#include <QtWidgets>

Q_DECLARE_METATYPE(BuildResult)

namespace {

constexpr int timeoutMs = 30000;

void writeText(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

// Clicks a button in the next modal dialog, from inside its exec() loop.
void answerNextDialog(QMessageBox::StandardButton button)
{
    auto *timer = new QTimer(qApp);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, button] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            timer->stop();
            timer->deleteLater();
            box->button(button)->click();
        }
    });
    timer->start();
}

} // namespace

class TestUi : public QObject
{
    Q_OBJECT

private:
    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<Settings> m_settings;
    QScopedPointer<MainWindow> m_window;

    QAction *action(const QString &name)
    {
        auto *found = m_window->findChild<QAction *>(QStringLiteral("action.") + name);
        if (!found)
            qFatal("no action named %s", qPrintable(name));
        return found;
    }

    QString path(const QString &relative) const { return m_dir->filePath(relative); }
    TerminalView *terminal() { return m_window->outputPanel()->terminal(); }
    CodeEditor *editor() { return m_window->editors()->currentEditor(); }

    void screenshot(const QString &name)
    {
        const QByteArray directory = qgetenv("LIGHTIDE_SCREENSHOT_DIR");
        if (directory.isEmpty())
            return;
        QTest::qWait(60);
        m_window->grab().save(QString::fromLocal8Bit(directory) + QLatin1Char('/') + name + ".png");
    }

    bool waitForDetection()
    {
        QElapsedTimer timer;
        timer.start();
        while (m_window->compilers()->detecting()) {
            if (timer.elapsed() > timeoutMs)
                return false;
            QTest::qWait(5);
        }
        return true;
    }

    BuildResult triggerBuild(const QString &actionName)
    {
        QSignalSpy reported(m_window.data(), &MainWindow::buildReported);
        action(actionName)->trigger();
        if (reported.isEmpty() && !reported.wait(timeoutMs))
            qFatal("build did not finish");
        return reported.at(0).at(0).value<BuildResult>();
    }

    bool waitForTerminal(const QString &text)
    {
        QElapsedTimer timer;
        timer.start();
        while (!terminal()->plainContents().contains(text)) {
            if (timer.elapsed() > timeoutMs)
                return false;
            QTest::qWait(5);
        }
        return true;
    }

    bool waitForProgramEnd()
    {
        QElapsedTimer timer;
        timer.start();
        while (m_window->runner()->isRunning()) {
            if (timer.elapsed() > timeoutMs)
                return false;
            QTest::qWait(5);
        }
        return true;
    }

    bool waitForDebugState(DebuggerManager::State state)
    {
        QElapsedTimer timer;
        timer.start();
        while (m_window->debugger()->state() != state) {
            if (timer.elapsed() > timeoutMs)
                return false;
            QTest::qWait(5);
        }
        return true;
    }

    bool waitForVariable(const QString &name, const QString &value)
    {
        QTreeWidget *view = m_window->outputPanel()->debugPanel()->variablesView();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            for (int i = 0; i < view->topLevelItemCount(); ++i) {
                if (view->topLevelItem(i)->text(0) == name && view->topLevelItem(i)->text(1) == value)
                    return true;
            }
            QTest::qWait(5);
        }
        return false;
    }

    CodeEditor *open(const QString &relative, const QByteArray &source)
    {
        writeText(path(relative), source);
        return m_window->editors()->openFile(path(relative));
    }

private slots:
    void initTestCase() { qRegisterMetaType<BuildResult>(); }

    void init()
    {
        m_dir.reset(new QTemporaryDir);
        m_settings.reset(new Settings(m_dir->filePath("settings.ini")));
        m_settings->load();
        m_window.reset(new MainWindow(m_settings.data()));
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.data()));
        m_window->activateWindow();
        QVERIFY(waitForDetection());
    }

    void cleanup()
    {
        if (m_window) {
            for (CodeEditor *open : m_window->editors()->editors())
                open->setModified(false);
            m_window->close();
        }
        m_window.reset();
        m_settings.reset();
        m_dir.reset();
    }

    // --- Compile and run -----------------------------------------------------

    void helloWorldCompilesAndRuns()
    {
        screenshot("welcome");
        open("hello.cpp", "#include <iostream>\n"
                          "using namespace std;\n"
                          "\n"
                          "int main() {\n"
                          "    cout << \"Hello, World!\" << endl;\n"
                          "    return 0;\n"
                          "}\n");

        const BuildResult result = triggerBuild("compileRun");
        QVERIFY(result.success);
        QVERIFY(waitForTerminal("Hello, World!"));
        QVERIFY(waitForTerminal("exited with code 0"));

        const QString compilerText = m_window->outputPanel()->compilerOutput()->toPlainText();
        QVERIFY(compilerText.contains("g++"));
        QVERIFY(compilerText.contains("-std=c++17"));
        QVERIFY(compilerText.contains("Build succeeded"));
        QVERIFY(compilerText.contains("exit code 0"));
        QCOMPARE(m_window->outputPanel()->currentIndex(), int(OutputPanel::ApplicationTab));
        screenshot("hello-world");
    }

    void cProgramUsesGcc()
    {
        open("hello.c", "#include <stdio.h>\nint main(void) { puts(\"from C\"); return 0; }\n");
        const BuildResult result = triggerBuild("compileRun");
        QVERIFY(result.success);
        QVERIFY(waitForTerminal("from C"));
        QVERIFY(m_window->outputPanel()->compilerOutput()->toPlainText().contains("gcc"));
    }

    void newFileTemplatesCompile()
    {
        m_window->editors()->newDocument(SourceLanguage::Cpp);
        QVERIFY(editor()->text().contains("Hello, World!"));
        QVERIFY(editor()->isUntitled());
        QVERIFY(!editor()->isModified());
        m_window->editors()->newDocument(SourceLanguage::C);
        QVERIFY(editor()->text().contains("printf"));
        QVERIFY(editor()->displayName().endsWith(".c"));
    }

    void compileSavesTheFileFirst()
    {
        CodeEditor *e = open("save.cpp", "int main() { return 0; }\n");
        e->setText("int main() { return 7; }\n");
        QVERIFY(e->isModified());
        const BuildResult result = triggerBuild("compile");
        QVERIFY(result.success);
        QVERIFY(!e->isModified());
        QFile file(path("save.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("return 7"));
    }

    void unsavedHeaderInAnotherTabIsCompiled()
    {
        CodeEditor *header = open("value.h", "#define VALUE 1\n");
        header->setText("#define VALUE 42\n");
        QVERIFY(header->isModified());

        open("usesheader.cpp", "#include \"value.h\"\n#include <cstdio>\n"
                               "int main() { printf(\"%d\\n\", VALUE); }\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("42"));
        QVERIFY(!header->isModified());
    }

    void runRebuildsWhenSourceChanged()
    {
        CodeEditor *e = open("stale.cpp", "#include <cstdio>\nint main() { puts(\"old\"); }\n");
        triggerBuild("compile");
        e->setText("#include <cstdio>\nint main() { puts(\"new\"); }\n");
        const BuildResult result = triggerBuild("run");
        QVERIFY(result.success);
        QVERIFY(waitForTerminal("new"));
        QVERIFY(!terminal()->plainContents().contains("old"));
    }

    void releaseConfigurationOptimizes()
    {
        m_settings->compiler.configuration = BuildConfiguration::Release;
        open("rel.cpp", "int main() { return 0; }\n");
        QVERIFY(triggerBuild("compile").success);
        const QString text = m_window->outputPanel()->compilerOutput()->toPlainText();
        QVERIFY(text.contains("-O2"));
        QVERIFY(text.contains("-DNDEBUG"));
    }

    void customCompilerOptionsAreUsed()
    {
        m_settings->compiler.cxxStandard = "c++20";
        m_settings->compiler.additionalArgs = "-DGREETING=7";
        CodeEditor *e = open("opt.cpp", "#include <cstdio>\n"
                                        "int main() { printf(\"%d %ld\\n\", GREETING, __cplusplus); }\n");
        Q_UNUSED(e)
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("7 202002"));
    }

    void customOutputNameIsHonoured()
    {
        open("named.cpp", "int main() { return 0; }\n");
        m_settings->setRunOptions(editor()->filePath(), {QString(), QString(), "custom-tool"});
        QVERIFY(triggerBuild("compile").success);
        QVERIFY(QFileInfo::exists(path("custom-tool")));
    }

    void compileWhileRunningStopsTheProgramFirst()
    {
        open("busy.cpp", "#include <cstdio>\n"
                         "int main() { puts(\"up\"); fflush(stdout); for (;;) {} }\n");
        triggerBuild("compileRun");
        QVERIFY(waitForTerminal("up"));
        QVERIFY(m_window->runner()->isRunning());

        action("compile")->trigger();   // Stops the program, then builds.
        QVERIFY(waitForProgramEnd());
        QSignalSpy reported(m_window.data(), &MainWindow::buildReported);
        QVERIFY(reported.wait(timeoutMs));
        const BuildResult result = reported.at(0).at(0).value<BuildResult>();
        QVERIFY(result.success);   // Would be "Text file busy" if it still ran.
        QVERIFY(!m_window->runner()->isRunning());
    }

    // --- Errors and warnings -------------------------------------------------

    void errorsAreClickableAndMarkTheEditor()
    {
        CodeEditor *e = open("my code/bad file.cpp", "int main() {\n"
                                                     "    int ok = 1;\n"
                                                     "    return nope;\n"
                                                     "}\n");
        const BuildResult result = triggerBuild("compileRun");
        QVERIFY(!result.success);
        QVERIFY(result.errors >= 1);
        QVERIFY(result.exitCode != 0);
        QCOMPARE(m_window->outputPanel()->currentIndex(), int(OutputPanel::MessagesTab));
        QVERIFY(!m_window->runner()->isRunning());
        screenshot("compile-error");

        // A click in Build Messages lands on the exact line and column.
        e->setCursorPosition(0, 0);
        QTreeWidget *messages = m_window->outputPanel()->messages();
        QVERIFY(messages->topLevelItemCount() >= 1);
        QCOMPARE(messages->topLevelItem(0)->text(1), QString("bad file.cpp"));
        QCOMPARE(messages->topLevelItem(0)->text(2), QString("3"));
        emit messages->itemClicked(messages->topLevelItem(0), 0);
        int line, index;
        e->getCursorPosition(&line, &index);
        QCOMPARE(line, 2);
        QCOMPARE(index, 11);

        // So does a click on the raw compiler output line.
        e->setCursorPosition(0, 0);
        QPlainTextEdit *output = m_window->outputPanel()->compilerOutput();
        int errorBlock = -1;
        for (int i = 0; i < output->blockCount(); ++i) {
            if (output->document()->findBlockByNumber(i).text().contains("error:")) {
                errorBlock = i;
                break;
            }
        }
        QVERIFY(errorBlock >= 0);
        QVERIFY(m_window->outputPanel()->activateCompilerLine(errorBlock));
        e->getCursorPosition(&line, &index);
        QCOMPARE(line, 2);

        // Fixing the code clears the markers on the next build.
        e->setText("int main() { return 0; }\n");
        QVERIFY(triggerBuild("compile").success);
        QCOMPARE(messages->topLevelItemCount(), 0);
    }

    void columnsAreRightInTabIndentedFiles()
    {
        CodeEditor *e = open("tabs.cpp", "int main() {\n\treturn nope;\n}\n");
        QVERIFY(!triggerBuild("compile").success);
        const Diagnostic first = m_window->outputPanel()->diagnostics().first();
        QCOMPARE(first.line, 2);
        QCOMPARE(first.column, 9);   // One byte for the tab, not eight columns.
        Q_UNUSED(e)
    }

    void warningsDoNotFailTheBuild()
    {
        open("warn.cpp", "int main() {\n    int unused;\n    return 0;\n}\n");
        const BuildResult result = triggerBuild("compile");
        QVERIFY(result.success);
        QCOMPARE(result.warnings, 1);
        QCOMPARE(m_window->outputPanel()->currentIndex(), int(OutputPanel::MessagesTab));
        QVERIFY(m_window->outputPanel()->compilerOutput()->toPlainText().contains("1 warning"));
    }

    // --- Running programs ----------------------------------------------------

    void interactiveInputWithCin()
    {
        open("cin.cpp", "#include <iostream>\n"
                        "#include <string>\n"
                        "using namespace std;\n"
                        "int main() {\n"
                        "    string name;\n"
                        "    int age;\n"
                        "    cout << \"Name: \";\n"
                        "    cin >> name;\n"
                        "    cout << \"Age: \";\n"
                        "    cin >> age;\n"
                        "    cout << name << \" is \" << age << endl;\n"
                        "}\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("Name: "));
        QVERIFY(terminal()->inputEnabled());
        QCOMPARE(QApplication::focusWidget(), terminal());

        QTest::keyClicks(terminal(), "Ada");
        QTest::keyClick(terminal(), Qt::Key_Return);
        QVERIFY(waitForTerminal("Age: "));

        // A typo fixed with Backspace goes through the terminal's line editing.
        QTest::keyClicks(terminal(), "37");
        QTest::keyClick(terminal(), Qt::Key_Backspace);
        QTest::keyClicks(terminal(), "6");
        QTest::keyClick(terminal(), Qt::Key_Return);

        QVERIFY(waitForTerminal("Ada is 36"));
        QVERIFY(waitForProgramEnd());
        QVERIFY(terminal()->plainContents().contains("Name: Ada"));
        QVERIFY(!terminal()->inputEnabled());
        screenshot("interactive");
    }

    void cStdioPromptShowsBeforeInput()
    {
        open("prompt.c", "#include <stdio.h>\n"
                         "int main(void) {\n"
                         "    int n;\n"
                         "    printf(\"Enter a number: \");\n"
                         "    scanf(\"%d\", &n);\n"
                         "    printf(\"doubled: %d\\n\", n * 2);\n"
                         "    return 0;\n"
                         "}\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("Enter a number: "));
        QTest::keyClicks(terminal(), "21");
        QTest::keyClick(terminal(), Qt::Key_Return);
        QVERIFY(waitForTerminal("doubled: 42"));
    }

    void commandLineArguments()
    {
        open("args.cpp", "#include <cstdio>\n"
                         "int main(int c, char **v) { for (int i = 1; i < c; i++) printf(\"<%s>\\n\", v[i]); }\n");
        m_settings->setRunOptions(editor()->filePath(), {"one \"two words\"", QString(), QString()});
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("<one>"));
        QVERIFY(waitForTerminal("<two words>"));
    }

    void workingDirectoryOption()
    {
        QDir().mkpath(path("elsewhere"));
        open("cwd.cpp", "#include <unistd.h>\n#include <cstdio>\n"
                        "int main() { char b[4096]; puts(getcwd(b, sizeof b)); }\n");
        m_settings->setRunOptions(editor()->filePath(), {QString(), path("elsewhere"), QString()});
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("elsewhere"));
    }

    void nonZeroExitCode()
    {
        open("code.cpp", "int main() { return 42; }\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("exited with code 42"));
    }

    void crashIsReported()
    {
        open("crash.cpp", "int main() { *(volatile int *)0 = 1; return 0; }\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("Segmentation fault"));
        QVERIFY(!m_window->runner()->isRunning());
        screenshot("crash");
    }

    void infiniteLoopCanBeStoppedAndUiStaysResponsive()
    {
        open("loop.cpp", "#include <cstdio>\n"
                         "int main() { for (long i = 0;; i++) printf(\"line %ld\\n\", i); }\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("line 1"));
        QVERIFY(action("stop")->isEnabled());

        int ticks = 0;
        QTimer ticker;
        connect(&ticker, &QTimer::timeout, this, [&] { ++ticks; });
        ticker.start(10);
        QTest::qWait(1000);
        QVERIFY2(ticks >= 40, qPrintable(QString("the event loop ran only %1 times").arg(ticks)));

        action("stop")->trigger();
        QVERIFY(waitForProgramEnd());
        QVERIFY(waitForTerminal("Stopped by user"));
        QVERIFY(!action("stop")->isEnabled());
    }

    void outputWithoutNewlinesStaysResponsive()
    {
        open("nonl.cpp", "#include <cstdio>\n"
                         "int main() { puts(\"go\"); for (;;) { printf(\"x\"); fflush(stdout); } }\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("go"));

        int ticks = 0;
        QTimer ticker;
        connect(&ticker, &QTimer::timeout, this, [&] { ++ticks; });
        ticker.start(10);
        QTest::qWait(1500);
        QVERIFY2(ticks >= 60, qPrintable(QString("the event loop ran only %1 times").arg(ticks)));

        action("stop")->trigger();
        QVERIFY(waitForProgramEnd());
        const QTextDocument *document = terminal()->document();
        for (QTextBlock block = document->begin(); block.isValid(); block = block.next())
            QVERIFY(block.length() <= 4097);
    }

    void carriageReturnProgressOverwritesTheLine()
    {
        open("prog.cpp", "#include <cstdio>\n"
                         "int main() { printf(\"10%%\\r20%%\\rdone\\n\"); return 0; }\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("exited"));
        const QString text = terminal()->plainContents();
        QVERIFY(text.contains("done"));
        QVERIFY(!text.contains("10%"));
    }

    void uiStaysResponsiveDuringCompilation()
    {
        open("slow.cpp", "#include <regex>\n#include <iostream>\n#include <map>\n"
                         "int main() { std::regex r(\"a+\"); std::cout << 1; }\n");
        int ticks = 0;
        QTimer ticker;
        connect(&ticker, &QTimer::timeout, this, [&] { ++ticks; });
        ticker.start(10);

        QElapsedTimer timer;
        timer.start();
        QVERIFY(triggerBuild("compile").success);
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(ticks >= elapsed / 25,
                 qPrintable(QString("%1 ticks in %2 ms").arg(ticks).arg(elapsed)));
    }

    void pathsWithSpacesWorkEndToEnd()
    {
        open("dir with spaces/my program.cpp",
             "#include <iostream>\nint main() { std::cout << \"spaced ok\\n\"; }\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("spaced ok"));
    }

    // --- Missing tools -------------------------------------------------------

    void missingCompilerShowsInstallInstructions()
    {
        m_settings->compiler.gxxPath = "/nonexistent/g++";
        m_window->compilers()->detect(m_settings->compiler);
        QVERIFY(waitForDetection());
        QTest::qWait(10);
        QVERIFY(m_window->outputPanel()->compilerOutput()->toPlainText().contains("build-essential"));

        open("x.cpp", "int main() { return 0; }\n");

        QString dialogText;
        auto *timer = new QTimer(this);
        timer->setInterval(10);
        connect(timer, &QTimer::timeout, this, [&, timer] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                timer->stop();
                dialogText = box->informativeText();
                box->accept();
            }
        });
        timer->start();
        action("compile")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!dialogText.isEmpty(), 5000);
        QVERIFY(dialogText.contains("sudo apt install build-essential gdb"));
        QVERIFY(!m_window->buildProcess()->isRunning());
    }

    void missingGdbExplainsItself()
    {
        m_settings->compiler.gdbPath = "/nonexistent/gdb";
        m_window->compilers()->detect(m_settings->compiler);
        QVERIFY(waitForDetection());
        open("x.cpp", "int main() { return 0; }\n");
        answerNextDialog(QMessageBox::Ok);
        action("debug")->trigger();
        QVERIFY(!m_window->debugger()->isActive());
        QVERIFY(!m_window->buildProcess()->isRunning());
    }

    // --- Unsaved changes -----------------------------------------------------

    void closingUnsavedFilePromptsAndCanCancel()
    {
        CodeEditor *e = open("u.cpp", "int a;\n");
        e->insertAt("// edit\n", 0, 0);
        QVERIFY(e->isModified());
        QVERIFY(m_window->editors()->tabText(0).contains(QStringLiteral("●")));

        answerNextDialog(QMessageBox::Cancel);
        QVERIFY(!m_window->editors()->closeEditor(e));
        QCOMPARE(m_window->editors()->count(), 1);

        answerNextDialog(QMessageBox::Save);
        QVERIFY(m_window->editors()->closeEditor(e));
        QCOMPARE(m_window->editors()->count(), 0);
        QFile file(path("u.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("// edit"));
    }

    void discardKeepsTheFileOnDiskUnchanged()
    {
        CodeEditor *e = open("d.cpp", "int a;\n");
        e->insertAt("// lost\n", 0, 0);
        answerNextDialog(QMessageBox::Discard);
        QVERIFY(m_window->editors()->closeEditor(e));
        QFile file(path("d.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("int a;\n"));
    }

    void quittingPromptsForEachUnsavedFileAndCanBeCancelled()
    {
        CodeEditor *e = open("q.cpp", "int a;\n");
        e->insertAt("x", 0, 0);

        answerNextDialog(QMessageBox::Cancel);
        QVERIFY(!m_window->close());
        QVERIFY(m_window->isVisible());
        QVERIFY(e->isModified());

        answerNextDialog(QMessageBox::Discard);
        QVERIFY(m_window->close());
    }

    void savingClearsTheModifiedMarker()
    {
        CodeEditor *e = open("m.cpp", "int a;\n");
        e->insertAt("x", 0, 0);
        action("save")->trigger();
        QVERIFY(!e->isModified());
        QVERIFY(!m_window->editors()->tabText(0).contains(QStringLiteral("●")));
    }

    void saveAllWritesEveryModifiedFile()
    {
        CodeEditor *a = open("a.cpp", "int a;\n");
        CodeEditor *b = open("b.cpp", "int b;\n");
        a->insertAt("// a\n", 0, 0);
        b->insertAt("// b\n", 0, 0);
        action("saveAll")->trigger();
        QVERIFY(!a->isModified());
        QVERIFY(!b->isModified());
    }

    void externalChangeReloadsCleanFile()
    {
        CodeEditor *e = open("ext.cpp", "int before;\n");
        QTest::qWait(30);
        QFile file(path("ext.cpp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("int after_external_edit;\n");
        file.setFileTime(QDateTime::currentDateTime().addSecs(3), QFileDevice::FileModificationTime);
        file.close();
        QTRY_VERIFY_WITH_TIMEOUT(e->text().contains("after_external_edit"), 5000);
        QVERIFY(!e->isModified());
    }

    // --- Files ---------------------------------------------------------------

    void opensEveryCppExtensionAndFocusesExistingTab()
    {
        for (const char *name : {"a.c", "a.cpp", "a.cc", "a.cxx", "a.h", "a.hpp", "a.hh", "a.hxx"})
            QVERIFY2(open(name, "int a;\n"), name);
        QCOMPARE(m_window->editors()->count(), 8);

        QCOMPARE(m_window->editors()->openFile(path("a.cpp")),
                 m_window->editors()->editorForPath(path("a.cpp")));
        QCOMPARE(m_window->editors()->count(), 8);
        QCOMPARE(editor()->filePath(), QFileInfo(path("a.cpp")).canonicalFilePath());
    }

    void recentFilesAreRemembered()
    {
        open("r.cpp", "int a;\n");
        QCOMPARE(m_settings->recentFiles().first(), QFileInfo(path("r.cpp")).canonicalFilePath());
    }

    void droppingFilesOpensThem()
    {
        writeText(path("dropped file.cpp"), "int dropped;\n");
        QMimeData data;
        data.setUrls({QUrl::fromLocalFile(path("dropped file.cpp"))});

        const QPoint centre = m_window->rect().center();
        QDragEnterEvent enter(centre, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(m_window.data(), &enter);
        QVERIFY(enter.isAccepted());

        QDropEvent drop(centre, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(m_window.data(), &drop);
        QVERIFY(editor());
        QVERIFY(editor()->text().contains("dropped"));
    }

    void lineEndingsSurviveASave()
    {
        CodeEditor *e = open("crlf.cpp", "int a;\r\nint b;\r\n");
        QCOMPARE(e->lineEnding(), EndOfLine::Windows);
        e->insert("// edit\r\n");
        action("save")->trigger();
        QFile file(path("crlf.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.contains("int a;\r\n"));
        QVERIFY(bytes.contains("int b;\r\n"));
    }

    // --- Editor behaviour ----------------------------------------------------

    void autoIndentAndBracePairs()
    {
        CodeEditor *e = open("i.cpp", "");
        e->setFocus();
        QTest::keyClicks(e, "int main() {");
        QCOMPARE(e->text(), QString("int main() {}"));

        // Enter between the braces opens an indented line.
        QTest::keyClick(e, Qt::Key_Return);
        QCOMPARE(e->text(), QString("int main() {\n    \n}"));
        QTest::keyClicks(e, "return 0;");
        QCOMPARE(e->text(), QString("int main() {\n    return 0;\n}"));

        // The next line keeps the indentation.
        QTest::keyClick(e, Qt::Key_Return);
        QTest::keyClicks(e, "x");
        QVERIFY(e->text(2).startsWith("    x"));
    }

    void closingBracketTypesOverTheAutoInsertedOne()
    {
        CodeEditor *e = open("t.cpp", "");
        e->setFocus();
        QTest::keyClicks(e, "f(a[1]);");
        QCOMPARE(e->text(), QString("f(a[1]);"));
    }

    void quotesPairOutsideStringsOnly()
    {
        CodeEditor *e = open("q.cpp", "");
        e->setFocus();
        QTest::keyClicks(e, "s = \"it's\";");
        QCOMPARE(e->text(), QString("s = \"it's\";"));
    }

    void tabSizeAndSpacesFollowSettings()
    {
        m_settings->editor.tabWidth = 2;
        m_window->editors()->applySettings();
        CodeEditor *e = open("tab.cpp", "");
        e->setFocus();
        QTest::keyClick(e, Qt::Key_Tab);
        QCOMPARE(e->text(), QString("  "));

        m_settings->editor.insertSpaces = false;
        e->applySettings(*m_settings);
        e->clear();
        QTest::keyClick(e, Qt::Key_Tab);
        QCOMPARE(e->text(), QString("\t"));
    }

    void foldingLineNumbersAndWrapFollowSettings()
    {
        CodeEditor *e = open("f.cpp", "int main() {\n    return 0;\n}\n");
        QVERIFY(e->folding() != QsciScintilla::NoFoldStyle);
        QVERIFY(e->marginWidth(0) > 0);

        m_settings->editor.codeFolding = false;
        m_settings->editor.lineNumbers = false;
        m_settings->editor.wordWrap = true;
        e->applySettings(*m_settings);
        QCOMPARE(e->folding(), QsciScintilla::NoFoldStyle);
        QCOMPARE(e->marginWidth(0), 0);
        QCOMPARE(e->wrapMode(), QsciScintilla::WrapWord);
    }

    void togglesLineComments()
    {
        CodeEditor *e = open("c.cpp", "int a;\n    int b;\n");
        e->selectAll();
        e->toggleLineComment();
        QCOMPARE(e->text(), QString("// int a;\n    // int b;\n"));
        e->selectAll();
        e->toggleLineComment();
        QCOMPARE(e->text(), QString("int a;\n    int b;\n"));
    }

    void findAndReplace()
    {
        CodeEditor *e = open("find.cpp", "alpha beta alpha gamma alpha\n");
        FindBar *bar = m_window->findBar();
        action("find")->trigger();
        QVERIFY(bar->isVisible());

        bar->setSearchText("alpha");
        QCOMPARE(e->selectedText(), QString("alpha"));
        QVERIFY(bar->findNext());
        int lineFrom, indexFrom, lineTo, indexTo;
        e->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
        QCOMPARE(indexFrom, 11);
        QVERIFY(bar->findPrevious());
        e->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
        QCOMPARE(indexFrom, 0);

        bar->setReplaceText("omega");
        QCOMPARE(bar->replaceAll(), 3);
        QCOMPARE(e->text(), QString("omega beta omega gamma omega\n"));

        // Replace All is one undo step.
        e->undo();
        QCOMPARE(e->text(), QString("alpha beta alpha gamma alpha\n"));
    }

    void replaceAllTerminatesWhenReplacementContainsTheNeedle()
    {
        CodeEditor *e = open("loop.cpp", "a a\n");
        FindBar *bar = m_window->findBar();
        bar->setEditor(e);
        action("replace")->trigger();
        bar->setSearchText("a");
        bar->setReplaceText("aa");
        QCOMPARE(bar->replaceAll(), 2);
        QCOMPARE(e->text(), QString("aa aa\n"));
    }

    void keyboardShortcutsMatchTheSpecification()
    {
        const QList<QPair<const char *, QKeySequence>> expected = {
            {"newCpp", QKeySequence("Ctrl+N")},   {"open", QKeySequence("Ctrl+O")},
            {"save", QKeySequence("Ctrl+S")},     {"saveAll", QKeySequence("Ctrl+Shift+S")},
            {"closeTab", QKeySequence("Ctrl+W")}, {"find", QKeySequence("Ctrl+F")},
            {"replace", QKeySequence("Ctrl+H")},  {"compile", QKeySequence("F9")},
            {"run", QKeySequence("F10")},         {"compileRun", QKeySequence("F11")},
            {"debug", QKeySequence("F5")},        {"stop", QKeySequence("Shift+F5")},
            {"build", QKeySequence("Ctrl+F9")},
        };
        for (const auto &[name, sequence] : expected)
            QCOMPARE(action(name)->shortcut(), sequence);
    }

    void shortcutsWorkWhileTheEditorHasFocus()
    {
        // QScintilla binds many Ctrl chords itself; these must reach the window.
        CodeEditor *e = open("s.cpp", "int a;\n");
        e->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(m_window.data()));

        QTest::keyClick(e, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(m_window->findBar()->isVisible());
        m_window->findBar()->hide();

        e->setFocus();
        QTest::keyClick(e, Qt::Key_H, Qt::ControlModifier);
        QVERIFY(m_window->findBar()->isVisible());
        m_window->findBar()->hide();

        e->setFocus();
        e->insertAt("x", 0, 0);
        QTest::keyClick(e, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!e->isModified());

        e->setFocus();
        QTest::keyClick(e, Qt::Key_B, Qt::ControlModifier);
        QVERIFY(!m_window->sidebar()->isVisible());
        QTest::keyClick(e, Qt::Key_B, Qt::ControlModifier);
        QVERIFY(m_window->sidebar()->isVisible());

        QSignalSpy reported(m_window.data(), &MainWindow::buildReported);
        e->setFocus();
        QTest::keyClick(e, Qt::Key_F9);
        QVERIFY(reported.wait(timeoutMs));
    }

    void ctrlCInTerminalInterruptsButF5StaysWithTheIde()
    {
        open("int.cpp", "#include <cstdio>\n"
                        "int main() { puts(\"up\"); fflush(stdout); for (;;) {} }\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("up"));
        QVERIFY(QTest::qWaitForWindowActive(m_window.data()));
        terminal()->setFocus();

        QTest::keyClick(terminal(), Qt::Key_C, Qt::ControlModifier);
        QVERIFY(waitForProgramEnd());
        QVERIFY(waitForTerminal("signal 2"));
    }

    void goToDefinitionAndSymbols()
    {
        CodeEditor *e = open("sym.cpp", "int helper(int x) {\n"
                                        "    return x;\n"
                                        "}\n"
                                        "\n"
                                        "int main() {\n"
                                        "    return helper(1);\n"
                                        "}\n");
        e->setCursorPosition(5, 13);
        action("goToDefinition")->trigger();
        int line, index;
        e->getCursorPosition(&line, &index);
        QCOMPARE(line, 0);

        m_window->sidebar()->setCurrentIndex(Sidebar::SymbolsTab);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->sidebar()->symbolsList()->count(), 2, 3000);
        QVERIFY(m_window->sidebar()->symbolsList()->item(1)->text().contains("main"));
    }

    // --- Debugging -----------------------------------------------------------

    void debuggingWithBreakpointsSteppingAndVariables()
    {
        CodeEditor *e = open("my debug/dbg app.cpp", "#include <iostream>\n"
                                                     "\n"
                                                     "int twice(int value) {\n"
                                                     "    int result = value * 2;\n"
                                                     "    return result;\n"
                                                     "}\n"
                                                     "\n"
                                                     "int main() {\n"
                                                     "    int start = 21;\n"
                                                     "    int answer = twice(start);\n"
                                                     "    std::cout << answer << std::endl;\n"
                                                     "    return 0;\n"
                                                     "}\n");
        e->toggleBreakpoint(10);
        QCOMPARE(e->breakpointLines(), QList<int>{10});

        triggerBuild("debug");
        QVERIFY(waitForDebugState(DebuggerManager::State::Paused));
        QCOMPARE(m_window->outputPanel()->currentIndex(), int(OutputPanel::DebugTab));
        QVERIFY(waitForVariable("start", "21"));
        QCOMPARE(action("debug")->text().remove('&'), QString("Continue"));
        QVERIFY(action("stepOver")->isEnabled());
        QVERIFY(action("stepOver")->isVisible());

        int line, index;
        e->getCursorPosition(&line, &index);
        QCOMPARE(line, 9);
        screenshot("debugging");

        action("stepInto")->trigger();
        QVERIFY(waitForVariable("value", "21"));
        QTreeWidget *stack = m_window->outputPanel()->debugPanel()->framesView();
        QCOMPARE(stack->topLevelItem(0)->text(1), QString("twice"));
        QVERIFY(stack->topLevelItem(0)->text(2).contains("dbg app.cpp:4"));

        action("stepOut")->trigger();
        QVERIFY(waitForDebugState(DebuggerManager::State::Paused));
        action("stepOver")->trigger();
        QVERIFY(waitForVariable("answer", "42"));

        action("debug")->trigger();   // F5 continues while paused.
        QVERIFY(waitForDebugState(DebuggerManager::State::Idle));
        QVERIFY(waitForTerminal("42"));
        QVERIFY(waitForTerminal("exited with code 0"));
        QVERIFY(!action("stepOver")->isVisible());
    }

    void debuggingWithoutBreakpointsStopsAtMain()
    {
        open("m.cpp", "int main() {\n    return 0;\n}\n");
        triggerBuild("debug");
        QVERIFY(waitForDebugState(DebuggerManager::State::Paused));
        action("stop")->trigger();
        QVERIFY(waitForDebugState(DebuggerManager::State::Idle));
    }

    void debugAlwaysBuildsWithSymbols()
    {
        m_settings->compiler.configuration = BuildConfiguration::Release;
        open("rel.cpp", "int main() {\n    int a = 1;\n    return a - 1;\n}\n");
        triggerBuild("debug");
        QVERIFY(waitForDebugState(DebuggerManager::State::Paused));
        const QString text = m_window->outputPanel()->compilerOutput()->toPlainText();
        QVERIFY(text.contains(" -g "));
        action("stop")->trigger();
        QVERIFY(waitForDebugState(DebuggerManager::State::Idle));
    }

    void programInputWorksWhileDebugging()
    {
        CodeEditor *e = open("in.cpp", "#include <iostream>\n"
                                       "int main() {\n"
                                       "    int n;\n"
                                       "    std::cin >> n;\n"
                                       "    std::cout << n + 1 << std::endl;\n"
                                       "}\n");
        e->toggleBreakpoint(4);
        triggerBuild("debug");
        QVERIFY(waitForDebugState(DebuggerManager::State::Paused));
        action("debug")->trigger();
        QVERIFY(waitForDebugState(DebuggerManager::State::Running));
        QVERIFY(terminal()->inputEnabled());
        QTest::keyClicks(terminal(), "41");
        QTest::keyClick(terminal(), Qt::Key_Return);
        QVERIFY(waitForTerminal("42"));
        QVERIFY(waitForDebugState(DebuggerManager::State::Idle));
    }

    // --- Projects ------------------------------------------------------------

    void multiFileProjectBuildsAndRuns()
    {
        writeText(path("proj/src/main.cpp"), "#include \"math_utils.h\"\n"
                                             "#include <iostream>\n"
                                             "int main() { std::cout << \"sum \" << add(40, 2) << std::endl; }\n");
        writeText(path("proj/src/math_utils.cpp"), "#include \"math_utils.h\"\n"
                                                   "int add(int a, int b) { return a + b; }\n");
        writeText(path("proj/include/math_utils.h"), "int add(int a, int b);\n");

        Project project = Project::create(path("proj"), "demo", true);
        project.sources = {"src/main.cpp", "src/math_utils.cpp"};
        project.headers = {"include/math_utils.h"};
        project.includeDirs = {"include"};
        QString error;
        QVERIFY(project.save(&error));

        QVERIFY(m_window->openProject(project.filePath()));
        QVERIFY(m_window->project());
        QCOMPARE(m_window->sidebar()->currentIndex(), int(Sidebar::ProjectTab));
        QVERIFY(m_window->windowTitle().contains("demo"));
        QVERIFY(m_settings->recentProjects().contains(project.filePath()));
        screenshot("project");

        const BuildResult result = triggerBuild("compileRun");
        QVERIFY(result.success);
        QVERIFY(waitForTerminal("sum 42"));

        // Nothing changed, so nothing is rebuilt.
        QVERIFY(waitForProgramEnd());
        QVERIFY(triggerBuild("build").upToDate);

        action("clean")->trigger();
        QVERIFY(!QFileInfo::exists(m_window->project()->outputPath(BuildConfiguration::Debug)));
    }

    void looseFileBuildsOnItsOwnWhileAProjectIsOpen()
    {
        writeText(path("proj/main.cpp"), "int main() { return 1; }\n");
        Project project = Project::create(path("proj"), "demo", true);
        QString error;
        QVERIFY(project.save(&error));
        QVERIFY(m_window->openProject(project.filePath()));

        open("loose.cpp", "#include <cstdio>\nint main() { puts(\"loose file\"); }\n");
        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("loose file"));
    }

    void newProjectDialogCreatesAndOpensAProject()
    {
        auto *timer = new QTimer(this);
        timer->setInterval(10);
        connect(timer, &QTimer::timeout, this, [this, timer] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            timer->stop();
            const QList<QLineEdit *> edits = dialog->findChildren<QLineEdit *>();
            edits.at(0)->setText("fresh project");
            edits.at(1)->setText(path("workspace"));
            dialog->accept();
        });
        timer->start();

        action("newProject")->trigger();
        QVERIFY(m_window->project());
        QCOMPARE(m_window->project()->name, QString("fresh project"));
        QVERIFY(QFileInfo::exists(path("workspace/fresh project/fresh project.lideproj")));
        QVERIFY(editor());
        QCOMPARE(editor()->displayName(), QString("main.cpp"));

        QVERIFY(triggerBuild("compileRun").success);
        QVERIFY(waitForTerminal("Hello, World!"));
    }

    void projectFilesCanBeAddedAndRemoved()
    {
        writeText(path("p/main.cpp"), "int main() { return 0; }\n");
        writeText(path("p/extra.cpp"), "int extra;\n");
        Project created = Project::create(path("p"), "demo", true);
        QString error;
        QVERIFY(created.save(&error));
        QVERIFY(m_window->openProject(created.filePath()));

        m_window->project()->addFile(path("p/extra.cpp"));
        emit m_window->sidebar()->removeProjectFileRequested(path("p/extra.cpp"));
        QVERIFY(!m_window->project()->contains(path("p/extra.cpp")));
        QVERIFY(QFileInfo::exists(path("p/extra.cpp")));   // Removed from the project only.

        Project reloaded;
        QVERIFY(reloaded.load(created.filePath(), &error));
        QVERIFY(!reloaded.sources.contains("extra.cpp"));
    }

    void projectTreeGroupsFilesByFolder()
    {
        Project project = Project::create(path("p"), "demo", true);
        project.sources << "lib/net/socket.cpp";
        project.folders << "assets";
        QString error;
        writeText(path("p/main.cpp"), "");
        QVERIFY(project.save(&error));
        QVERIFY(m_window->openProject(project.filePath()));

        QTreeWidgetItem *root = m_window->sidebar()->projectTree()->topLevelItem(0);
        QCOMPARE(root->text(0), QString("demo"));
        QStringList names;
        for (int i = 0; i < root->childCount(); ++i)
            names << root->child(i)->text(0);
        QVERIFY(names.contains("assets"));
        QVERIFY(names.contains("lib"));
        QVERIFY(names.contains("main.cpp"));
    }

    // --- Find in files --------------------------------------------------------

    void replaceInFilesIsLiteral()
    {
        writeText(path("src/a.cpp"), "int name;\n");
        FindInFilesDialog dialog;
        dialog.setFolder(path("src"));
        dialog.setSearchText("name");
        QSignalSpy finished(&dialog, &FindInFilesDialog::searchFinished);
        dialog.startSearch();
        QVERIFY(finished.wait(5000));
        QCOMPARE(dialog.results()->topLevelItemCount(), 1);

        QCOMPARE(dialog.replaceInMatchedFiles("a\\1b"), 1);
        QFile file(path("src/a.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("int a\\1b;\n"));
    }

    // --- Appearance and settings ---------------------------------------------

    void themeSwitchPersists()
    {
        open("t.cpp", "int main() { return 0; }\n");
        const QColor darkPaper = editor()->lexer()->paper(0);
        action("toggleDark")->trigger();
        QCOMPARE(m_settings->appearance.theme, ThemeKind::Light);
        QVERIFY(editor()->lexer()->paper(0) != darkPaper);
        screenshot("light-theme");

        Settings reloaded(m_dir->filePath("settings.ini"));
        reloaded.load();
        QCOMPARE(reloaded.appearance.theme, ThemeKind::Light);
    }

    void settingsDialogAppliesAndPersists()
    {
        open("s.cpp", "int a;\n");
        SettingsDialog dialog(m_settings.data(), m_window.data());
        dialog.show();
        if (!qEnvironmentVariableIsEmpty("LIGHTIDE_SCREENSHOT_DIR")) {
            QTest::qWait(60);
            dialog.grab().save(qEnvironmentVariable("LIGHTIDE_SCREENSHOT_DIR") + "/settings.png");
        }
        auto *tabWidth = dialog.findChild<QSpinBox *>();
        QVERIFY(tabWidth);
        dialog.applyToSettings();

        m_settings->editor.tabWidth = 8;
        m_settings->save();
        emit m_settings->changed();
        QCOMPARE(editor()->tabWidth(), 8);
    }

    void panelsCollapseAndPersist()
    {
        action("togglePanel")->trigger();
        QVERIFY(!m_window->outputPanel()->isVisible());
        QVERIFY(!m_settings->appearance.bottomPanelVisible);
        action("togglePanel")->trigger();
        QVERIFY(m_window->outputPanel()->isVisible());

        action("toggleSidebar")->trigger();
        QVERIFY(!m_window->sidebar()->isVisible());
        action("toggleSidebar")->trigger();
        QVERIFY(m_window->sidebar()->isVisible());
    }

    void toolbarHasEverySpecifiedAction()
    {
        auto *toolBar = m_window->findChild<QToolBar *>("mainToolBar");
        QVERIFY(toolBar);
        const QList<QAction *> actions = toolBar->actions();
        for (const char *name : {"newCpp", "open", "save", "saveAll", "undo", "redo", "find",
                                 "compile", "run", "compileRun", "debug", "stop"}) {
            QVERIFY2(actions.contains(action(name)), name);
            QVERIFY2(!action(name)->icon().isNull(), name);
            QVERIFY2(!action(name)->toolTip().isEmpty(), name);
        }
    }

    void menusMatchTheSpecification()
    {
        QStringList titles;
        const QList<QAction *> menus = m_window->menuBar()->actions();
        for (QAction *menu : menus)
            titles << menu->text().remove('&');
        QCOMPARE(titles, QStringList({"File", "Edit", "Search", "View", "Project", "Build", "Debug",
                                      "Tools", "Help"}));
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
