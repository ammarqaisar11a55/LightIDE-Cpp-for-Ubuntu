#pragma once

#include "compiler/BuildProcess.h"
#include "compiler/CompileCommand.h"
#include "core/Settings.h"
#include "debugger/DebuggerManager.h"
#include "execution/ProcessRunner.h"
#include "leetcode/ProblemFolder.h"

#include <QHash>
#include <QMainWindow>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>

class CodeEditor;
class CompilerManager;
class EditorWidget;
class FindBar;
class FindInFilesDialog;
class LeetCodeClient;
class OutputPanel;
class Project;
class QComboBox;
class QLabel;
class QMenu;
class QSplitter;
class QStackedWidget;
class Sidebar;
class TestSuiteRunner;
class WelcomeWidget;
struct LcRunReport;
struct LcTestResult;
struct LeetCodeProblem;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Settings *settings, QWidget *parent = nullptr);
    ~MainWindow() override;

    void openPaths(const QStringList &paths);
    bool openProject(const QString &path);
    void openFolder(const QString &path);
    void closeFolder();
    QString currentFolder() const;

    EditorWidget *editors() const { return m_editors; }
    OutputPanel *outputPanel() const { return m_output; }
    Sidebar *sidebar() const { return m_sidebar; }
    FindBar *findBar() const { return m_findBar; }
    BuildProcess *buildProcess() const { return m_build; }
    ProcessRunner *runner() const { return m_runner; }
    DebuggerManager *debugger() const { return m_debugger; }
    CompilerManager *compilers() const { return m_compilers; }
    Project *project() const { return m_project.get(); }
    LeetCodeClient *leetCodeClient() const { return m_leetCodeClient; }
    TestSuiteRunner *testRunner() const { return m_testRunner; }

    void fetchProblem(const QString &reference);
    void runLeetCodeTests(const QList<int> &onlyTests = {});

public slots:
    void compile();
    void compileAndRun();
    void run();
    void startOrContinueDebugging();
    void buildProject();
    void rebuildAll();
    void clean();
    void stop();

signals:
    // Fired after a build's results have reached the panels, for tests and tools.
    void buildReported(const BuildResult &result);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum class AfterBuild { Nothing, Run, Debug, LeetCodeTests, LeetCodeDebug };

    struct RunTarget
    {
        QString program;
        QStringList arguments;
        QString argumentText;
        QString workingDirectory;
    };

    // UI construction.
    QAction *addAction(const QString &name, const QString &text, const QString &icon,
                       const QKeySequence &shortcut, std::function<void()> handler);
    void createActions();
    void createLeetCodeActions();
    void createMenus();
    void createToolBar();
    void createCentralArea();
    void createStatusBar();
    void connectComponents();

    QAction *action(const QString &name) const { return m_actions.value(name); }

    // State reflected into the UI.
    void applySettings();
    void applyIconColors();
    void updateActions();
    void updateWindowTitle();
    void updateCursorLabel(int line, int column);
    void updateDocumentLabels();
    void updateToolLabel();
    void detectToolsIfChanged();
    void rebuildRecentMenus();
    void showCentralPage();
    void refreshSymbols();
    void showPanel(int tab);
    void setPanelVisible(bool visible);
    void setSidebarVisible(bool visible);

    // Files and projects.
    void openFileDialog();
    void openFolderDialog();
    void showFolderInSidebar();
    void newProject();
    void openProjectDialog();
    void closeProject();
    void addFilesToProject();
    void newProjectFile(const QString &folder);
    void newProjectFolder(const QString &parentFolder);
    void removeProjectFile(const QString &path);
    void showProjectOptions();
    void saveProject();
    void setProject(std::unique_ptr<Project> project);

    // Build, run and debug.
    bool usesProject() const;
    void startBuild(AfterBuild after, BuildConfiguration configuration);
    std::optional<BuildPlan> makeSingleFilePlan(CodeEditor *editor, BuildConfiguration configuration);
    bool checkCompilerFor(bool needsGxx, bool needsGcc);
    void onBuildFinished(const BuildResult &result);
    std::optional<RunTarget> currentRunTarget();
    bool runTargetIsStale(const RunTarget &target);
    void launch(const RunTarget &target);
    void beginDebugSession();
    void beginDebugSession(const RunTarget &target);
    void onProgramFinished(const RunResult &result);
    void onDebuggerPaused(const QString &file, int line, const QString &reason);
    void onDebugSessionEnded();
    QList<SourceLocation> collectBreakpoints() const;
    void whenIdle(std::function<void()> action);
    void showRunOptions();
    void showMissingToolMessage(const QString &toolName);
    void showCompilerStatus();

    // LeetCode.
    std::optional<ProblemFolder> currentProblem();
    LeetCodeRunSettings leetCodeRunSettings() const;
    CodeEditor *syncProblemEditor(const ProblemFolder &folder, const ProblemMetadata &metadata,
                                  const QList<LcTestCase> &tests);
    void buildProblem(AfterBuild after, const QList<int> &onlyTests);
    void startPlan(const BuildPlan &plan, AfterBuild after);
    void onProblemFetched(const LeetCodeProblem &problem);
    void onTestsFinished(const LcRunReport &report);
    void promptFetchProblem();
    void runCurrentTest();
    void debugSolution();
    void copySolution();
    void addCustomTest();
    void refreshProblem();
    void openProblemInBrowser();
    void editTests();
    void connectAccount();
    void disconnectAccount();
    void openProblemFolder(const QString &directory);
    void goToTest(int id);

    // Navigation.
    void goToLine();
    void goToDefinition();
    void goToLocation(const QString &file, int line, int column);
    void showFindInFiles();
    void showShortcuts();
    void showAbout();
    void adjustFontSize(int delta);

    Settings *m_settings;
    CompilerManager *m_compilers;
    BuildProcess *m_build;
    ProcessRunner *m_runner;
    DebuggerManager *m_debugger;
    std::unique_ptr<Project> m_project;

    EditorWidget *m_editors;
    WelcomeWidget *m_welcome;
    QStackedWidget *m_centralStack;
    FindBar *m_findBar;
    OutputPanel *m_output;
    Sidebar *m_sidebar;
    FindInFilesDialog *m_findInFiles = nullptr;
    LeetCodeClient *m_leetCodeClient;
    TestSuiteRunner *m_testRunner;
    std::optional<ProblemFolder> m_activeProblem;
    QString m_refreshDirectory;
    QList<int> m_onlyTests;
    QSplitter *m_horizontalSplitter;
    QSplitter *m_verticalSplitter;

    QHash<QString, QAction *> m_actions;
    QHash<QString, QString> m_actionIcons;
    QMenu *m_recentFilesMenu = nullptr;
    QMenu *m_recentProjectsMenu = nullptr;
    QMenu *m_recentFoldersMenu = nullptr;
    QComboBox *m_configurationBox = nullptr;
    QLabel *m_cursorLabel = nullptr;
    QLabel *m_eolLabel = nullptr;
    QLabel *m_indentLabel = nullptr;
    QLabel *m_languageLabel = nullptr;
    QLabel *m_toolLabel = nullptr;
    QLabel *m_leetCodeLabel = nullptr;
    QTimer m_symbolTimer;

    QStringList m_detectedToolPaths;
    AfterBuild m_afterBuild = AfterBuild::Nothing;
    QList<Diagnostic> m_buildDiagnostics;
    std::function<void()> m_whenIdle;
    bool m_debuggerOwnsTerminal = false;
    bool m_symbolsVisible = false;
};
