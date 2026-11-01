#include "ui/MainWindow.h"

#include "compiler/CompilerManager.h"
#include "core/SymbolScanner.h"
#include "editor/CodeEditor.h"
#include "filesystem/FileManager.h"
#include "leetcode/LeetCodeClient.h"
#include "leetcode/QuestionParser.h"
#include "leetcode/SolutionFile.h"
#include "leetcode/TestSuiteRunner.h"
#include "project/Project.h"
#include "ui/AddTestDialog.h"
#include "ui/DebugPanel.h"
#include "ui/EditorWidget.h"
#include "ui/FindBar.h"
#include "ui/FindInFilesDialog.h"
#include "ui/LeetCodePanel.h"
#include "ui/OutputPanel.h"
#include "ui/ProjectDialogs.h"
#include "ui/SettingsDialog.h"
#include "ui/Sidebar.h"
#include "ui/TerminalView.h"
#include "ui/Theme.h"
#include "ui/WelcomeWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int symbolRefreshDelayMs = 400;
constexpr int statusMessageMs = 6000;

QString formatSeconds(qint64 milliseconds)
{
    return QString::number(static_cast<double>(milliseconds) / 1000.0, 'f', 2);
}

QString eolName(EndOfLine eol)
{
    switch (eol) {
    case EndOfLine::Windows: return QStringLiteral("CRLF");
    case EndOfLine::ClassicMac: return QStringLiteral("CR");
    case EndOfLine::Unix: break;
    }
    return QStringLiteral("LF");
}

} // namespace

MainWindow::MainWindow(Settings *settings, QWidget *parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_compilers(new CompilerManager(this))
    , m_build(new BuildProcess(this))
    , m_runner(new ProcessRunner(this))
    , m_debugger(new DebuggerManager(this))
    , m_editors(new EditorWidget(settings))
    , m_welcome(new WelcomeWidget)
    , m_centralStack(new QStackedWidget)
    , m_findBar(new FindBar)
    , m_output(new OutputPanel)
    , m_sidebar(new Sidebar)
    , m_leetCodeClient(new LeetCodeClient(this))
    , m_testRunner(new TestSuiteRunner(this))
    , m_horizontalSplitter(new QSplitter(Qt::Horizontal))
    , m_verticalSplitter(new QSplitter(Qt::Vertical))
{
    setAcceptDrops(true);
    setWindowIcon(QIcon(QStringLiteral(":/lightide.svg")));
    resize(1180, 760);

    createActions();
    createLeetCodeActions();
    createMenus();
    createToolBar();
    createCentralArea();
    createStatusBar();
    connectComponents();

    QSettings &store = m_settings->store();
    restoreGeometry(store.value("window/geometry").toByteArray());
    m_horizontalSplitter->restoreState(store.value("window/horizontalSplitter").toByteArray());
    m_verticalSplitter->restoreState(store.value("window/verticalSplitter").toByteArray());

    const QString rootFolder = store.value("sidebar/rootFolder", QDir::homePath()).toString();
    m_sidebar->setRootFolder(rootFolder);

    applySettings();
    rebuildRecentMenus();
    showCentralPage();
    updateActions();
    updateWindowTitle();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

QAction *MainWindow::addAction(const QString &name, const QString &text, const QString &icon,
                               const QKeySequence &shortcut, std::function<void()> handler)
{
    auto *newAction = new QAction(text, this);
    newAction->setObjectName(QStringLiteral("action.") + name);
    newAction->setShortcut(shortcut);
    if (!shortcut.isEmpty()) {
        newAction->setToolTip(QStringLiteral("%1 (%2)").arg(
            QString(text).remove(QLatin1Char('&')).remove(QStringLiteral("...")),
            shortcut.toString(QKeySequence::NativeText)));
    }
    connect(newAction, &QAction::triggered, this, [handler = std::move(handler)] { handler(); });
    m_actions.insert(name, newAction);
    if (!icon.isEmpty())
        m_actionIcons.insert(name, icon);
    return newAction;
}

void MainWindow::createActions()
{
    const auto withEditor = [this](std::function<void(CodeEditor *)> handler) {
        return [this, handler = std::move(handler)] {
            if (CodeEditor *editor = m_editors->currentEditor())
                handler(editor);
        };
    };

    // File
    addAction("newCpp", tr("&New C++ File"), "new-file", QKeySequence::New, [this] {
        m_editors->newDocument(SourceLanguage::Cpp);
    });
    addAction("newC", tr("New &C File"), QString(), QKeySequence(tr("Ctrl+Shift+N")), [this] {
        m_editors->newDocument(SourceLanguage::C);
    });
    addAction("open", tr("&Open File..."), "open", QKeySequence::Open, [this] { openFileDialog(); });
    addAction("save", tr("&Save"), "save", QKeySequence::Save, withEditor([this](CodeEditor *e) {
        m_editors->save(e);
    }));
    addAction("saveAs", tr("Save &As..."), QString(), QKeySequence(), withEditor([this](CodeEditor *e) {
        m_editors->saveAs(e);
    }));
    addAction("saveAll", tr("Save A&ll"), "save-all", QKeySequence(tr("Ctrl+Shift+S")), [this] {
        m_editors->saveAll();
    });
    addAction("closeTab", tr("&Close File"), QString(), QKeySequence(tr("Ctrl+W")),
              withEditor([this](CodeEditor *e) { m_editors->closeEditor(e); }));
    addAction("closeAll", tr("Close All Files"), QString(), QKeySequence(), [this] {
        m_editors->closeAll();
    });
    addAction("quit", tr("&Quit"), QString(), QKeySequence::Quit, [this] { close(); });

    // Edit
    addAction("undo", tr("&Undo"), "undo", QKeySequence::Undo, withEditor([](CodeEditor *e) { e->undo(); }));
    addAction("redo", tr("&Redo"), "redo", QKeySequence::Redo, withEditor([](CodeEditor *e) { e->redo(); }));
    addAction("cut", tr("Cu&t"), QString(), QKeySequence::Cut, withEditor([](CodeEditor *e) { e->cut(); }));
    addAction("copy", tr("&Copy"), QString(), QKeySequence::Copy, withEditor([](CodeEditor *e) { e->copy(); }));
    addAction("paste", tr("&Paste"), QString(), QKeySequence::Paste, withEditor([](CodeEditor *e) { e->paste(); }));
    addAction("selectAll", tr("Select &All"), QString(), QKeySequence::SelectAll,
              withEditor([](CodeEditor *e) { e->selectAll(); }));
    addAction("toggleComment", tr("Toggle &Comment"), QString(), QKeySequence(tr("Ctrl+/")),
              withEditor([](CodeEditor *e) { e->toggleLineComment(); }));

    // Search
    addAction("find", tr("&Find..."), "find", QKeySequence::Find, [this] { m_findBar->showFind(); });
    addAction("replace", tr("&Replace..."), QString(), QKeySequence(tr("Ctrl+H")), [this] {
        m_findBar->showReplace();
    });
    addAction("findNext", tr("Find &Next"), QString(), QKeySequence(Qt::Key_F3), [this] {
        m_findBar->findNext();
    });
    addAction("findPrevious", tr("Find &Previous"), QString(), QKeySequence(Qt::SHIFT | Qt::Key_F3),
              [this] { m_findBar->findPrevious(); });
    addAction("findInFiles", tr("Find in F&iles..."), QString(), QKeySequence(tr("Ctrl+Shift+F")),
              [this] { showFindInFiles(); });
    addAction("goToLine", tr("&Go to Line..."), QString(), QKeySequence(tr("Ctrl+G")), [this] { goToLine(); });
    addAction("goToDefinition", tr("Go to &Definition"), QString(), QKeySequence(Qt::Key_F12), [this] {
        goToDefinition();
    });

    // View
    QAction *sidebarToggle = addAction("toggleSidebar", tr("&Sidebar"), "sidebar",
                                       QKeySequence(tr("Ctrl+B")), [] {});
    sidebarToggle->setCheckable(true);
    connect(sidebarToggle, &QAction::toggled, this, &MainWindow::setSidebarVisible);
    QAction *panelToggle = addAction("togglePanel", tr("&Bottom Panel"), "panel",
                                     QKeySequence(tr("Ctrl+J")), [] {});
    panelToggle->setCheckable(true);
    connect(panelToggle, &QAction::toggled, this, &MainWindow::setPanelVisible);
    QAction *wrapToggle = addAction("toggleWrap", tr("&Word Wrap"), QString(), QKeySequence(tr("Alt+Z")), [] {});
    wrapToggle->setCheckable(true);
    connect(wrapToggle, &QAction::toggled, this, [this](bool checked) {
        if (m_settings->editor.wordWrap == checked)
            return;
        m_settings->editor.wordWrap = checked;
        m_settings->save();
        m_editors->applySettings();
    });
    QAction *darkToggle = addAction("toggleDark", tr("&Dark Theme"), QString(), QKeySequence(), [] {});
    darkToggle->setCheckable(true);
    connect(darkToggle, &QAction::toggled, this, [this](bool checked) {
        const ThemeKind theme = checked ? ThemeKind::Dark : ThemeKind::Light;
        if (m_settings->appearance.theme == theme)
            return;
        m_settings->appearance.theme = theme;
        m_settings->save();
        applySettings();
    });
    addAction("zoomIn", tr("Zoom &In"), QString(), QKeySequence(tr("Ctrl+=")), [this] { adjustFontSize(1); });
    addAction("zoomOut", tr("Zoom &Out"), QString(), QKeySequence(tr("Ctrl+-")), [this] { adjustFontSize(-1); });

    // Project
    addAction("newProject", tr("&New Project..."), QString(), QKeySequence(), [this] { newProject(); });
    addAction("openProject", tr("&Open Project..."), QString(), QKeySequence(), [this] { openProjectDialog(); });
    addAction("closeProject", tr("&Close Project"), QString(), QKeySequence(), [this] { closeProject(); });
    addAction("addFiles", tr("&Add Files..."), QString(), QKeySequence(), [this] { addFilesToProject(); });
    addAction("newProjectFile", tr("New &File in Project..."), QString(), QKeySequence(), [this] {
        newProjectFile(QString());
    });
    addAction("projectOptions", tr("Project &Options..."), QString(), QKeySequence(), [this] {
        showProjectOptions();
    });

    // Build
    addAction("compile", tr("&Compile"), "compile", QKeySequence(Qt::Key_F9), [this] { compile(); });
    addAction("run", tr("&Run"), "run", QKeySequence(Qt::Key_F10), [this] { run(); });
    addAction("compileRun", tr("Compile && R&un"), "compile-run", QKeySequence(Qt::Key_F11), [this] {
        compileAndRun();
    });
    addAction("build", tr("&Build"), QString(), QKeySequence(tr("Ctrl+F9")), [this] { buildProject(); });
    addAction("rebuild", tr("Rebuild &All"), QString(), QKeySequence(tr("Ctrl+Shift+F9")), [this] { rebuildAll(); });
    addAction("clean", tr("C&lean"), QString(), QKeySequence(), [this] { clean(); });
    addAction("runOptions", tr("Run &Options..."), QString(), QKeySequence(), [this] { showRunOptions(); });
    addAction("compilerSettings", tr("Compiler &Settings..."), QString(), QKeySequence(), [this] {
        SettingsDialog dialog(m_settings, this);
        dialog.showPage(SettingsDialog::CompilerPage);
        dialog.exec();
    });

    // Debug
    addAction("debug", tr("Start &Debugging"), "debug", QKeySequence(Qt::Key_F5), [this] {
        startOrContinueDebugging();
    });
    addAction("stop", tr("&Stop"), "stop", QKeySequence(Qt::SHIFT | Qt::Key_F5), [this] { stop(); });
    addAction("stepOver", tr("Step &Over"), "step-over", QKeySequence(Qt::Key_F6), [this] {
        m_debugger->stepOver();
    });
    addAction("stepInto", tr("Step &Into"), "step-into", QKeySequence(Qt::Key_F7), [this] {
        m_debugger->stepInto();
    });
    addAction("stepOut", tr("Step O&ut"), "step-out", QKeySequence(Qt::SHIFT | Qt::Key_F7), [this] {
        m_debugger->stepOut();
    });
    addAction("toggleBreakpoint", tr("Toggle &Breakpoint"), QString(), QKeySequence(Qt::Key_F8),
              withEditor([](CodeEditor *e) {
                  int line, index;
                  e->getCursorPosition(&line, &index);
                  e->toggleBreakpoint(line + 1);
              }));

    // Tools and Help
    addAction("settings", tr("&Settings..."), QString(), QKeySequence(tr("Ctrl+,")), [this] {
        SettingsDialog dialog(m_settings, this);
        dialog.exec();
    });
    addAction("compilerStatus", tr("&Compiler Status..."), QString(), QKeySequence(), [this] {
        showCompilerStatus();
    });
    addAction("shortcuts", tr("&Keyboard Shortcuts"), QString(), QKeySequence(), [this] { showShortcuts(); });
    addAction("about", tr("&About LightIDE"), QString(), QKeySequence(), [this] { showAbout(); });
}

void MainWindow::createLeetCodeActions()
{
    addAction("lcFetch", tr("&Fetch Problem..."), QString(), QKeySequence(tr("Ctrl+Alt+F")),
              [this] { promptFetchProblem(); });
    addAction("lcRun", tr("&Run Tests"), QString(), QKeySequence(tr("Ctrl+Shift+B")),
              [this] { runLeetCodeTests(); });
    addAction("lcRunCurrent", tr("Run &Current Test"), QString(), QKeySequence(tr("Ctrl+Alt+R")),
              [this] { runCurrentTest(); });
    addAction("lcDebug", tr("&Debug Solution"), QString(), QKeySequence(), [this] { debugSolution(); });
    addAction("lcCopy", tr("&Copy Solution"), QString(), QKeySequence(tr("Ctrl+Alt+C")),
              [this] { copySolution(); });
    addAction("lcAddTest", tr("&Add Custom Test..."), QString(), QKeySequence(), [this] { addCustomTest(); });
    addAction("lcEditTests", tr("&Edit Tests (tests.json)"), QString(), QKeySequence(), [this] { editTests(); });
    addAction("lcRefresh", tr("Refres&h Problem"), QString(), QKeySequence(), [this] { refreshProblem(); });
    addAction("lcBrowser", tr("Open in &Browser"), QString(), QKeySequence(), [this] { openProblemInBrowser(); });
    addAction("lcConnect", tr("Connect &Account..."), QString(), QKeySequence(), [this] { connectAccount(); });
    addAction("lcDisconnect", tr("Dis&connect Account"), QString(), QKeySequence(), [this] { disconnectAccount(); });
}

void MainWindow::createMenus()
{
    const auto addAll = [this](QMenu *menu, const QStringList &names) {
        for (const QString &name : names) {
            if (name.isEmpty())
                menu->addSeparator();
            else
                menu->addAction(action(name));
        }
    };

    QMenu *file = menuBar()->addMenu(tr("&File"));
    addAll(file, {"newCpp", "newC", "open"});
    m_recentFilesMenu = file->addMenu(tr("Recent &Files"));
    addAll(file, {"", "save", "saveAs", "saveAll", "", "closeTab", "closeAll", "", "quit"});

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    addAll(edit, {"undo", "redo", "", "cut", "copy", "paste", "selectAll", "", "toggleComment"});
    edit->addSeparator();
    QMenu *lineEndings = edit->addMenu(tr("Line &Endings"));
    auto *endingsGroup = new QActionGroup(this);
    const QList<QPair<QString, EndOfLine>> endings = {
        {tr("Unix (LF)"), EndOfLine::Unix},
        {tr("Windows (CRLF)"), EndOfLine::Windows},
        {tr("Classic Mac (CR)"), EndOfLine::ClassicMac},
    };
    for (const auto &[label, eol] : endings) {
        QAction *ending = lineEndings->addAction(label);
        endingsGroup->addAction(ending);
        connect(ending, &QAction::triggered, this, [this, eol = eol] {
            if (CodeEditor *editor = m_editors->currentEditor()) {
                editor->setLineEnding(eol);
                editor->convertEols(editor->eolMode());
                updateDocumentLabels();
            }
        });
    }

    QMenu *search = menuBar()->addMenu(tr("&Search"));
    addAll(search, {"find", "replace", "findNext", "findPrevious", "", "findInFiles", "",
                    "goToLine", "goToDefinition"});

    QMenu *view = menuBar()->addMenu(tr("&View"));
    addAll(view, {"toggleSidebar", "togglePanel", "", "toggleWrap", "toggleDark", "", "zoomIn", "zoomOut"});

    QMenu *project = menuBar()->addMenu(tr("&Project"));
    addAll(project, {"newProject", "openProject"});
    m_recentProjectsMenu = project->addMenu(tr("Recent &Projects"));
    addAll(project, {"closeProject", "", "addFiles", "newProjectFile", "", "projectOptions"});

    QMenu *build = menuBar()->addMenu(tr("&Build"));
    addAll(build, {"compile", "run", "compileRun", "", "build", "rebuild", "clean", "", "stop", "",
                   "runOptions", "compilerSettings"});

    QMenu *debug = menuBar()->addMenu(tr("&Debug"));
    addAll(debug, {"debug", "stop", "", "stepOver", "stepInto", "stepOut", "", "toggleBreakpoint"});

    QMenu *leetCode = menuBar()->addMenu(tr("&LeetCode"));
    addAll(leetCode, {"lcFetch", "", "lcRun", "lcRunCurrent", "lcDebug", "", "lcCopy", "lcBrowser", "",
                      "lcAddTest", "lcEditTests", "lcRefresh", "", "lcConnect", "lcDisconnect"});

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    addAll(tools, {"settings", "compilerStatus"});

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    addAll(help, {"shortcuts", "about"});
}

void MainWindow::createToolBar()
{
    auto *toolBar = new QToolBar(tr("Main Toolbar"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setIconSize(QSize(18, 18));
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(toolBar);

    toolBar->addAction(action("newCpp"));
    toolBar->addAction(action("open"));
    toolBar->addAction(action("save"));
    toolBar->addAction(action("saveAll"));
    toolBar->addSeparator();
    toolBar->addAction(action("undo"));
    toolBar->addAction(action("redo"));
    toolBar->addAction(action("find"));
    toolBar->addSeparator();
    toolBar->addAction(action("compile"));
    toolBar->addAction(action("run"));
    toolBar->addAction(action("compileRun"));

    m_configurationBox = new QComboBox;
    m_configurationBox->addItem(tr("Debug"));
    m_configurationBox->addItem(tr("Release"));
    m_configurationBox->setToolTip(tr("Build configuration"));
    m_configurationBox->setFocusPolicy(Qt::ClickFocus);
    toolBar->addWidget(m_configurationBox);
    connect(m_configurationBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_settings->compiler.configuration =
            index == 1 ? BuildConfiguration::Release : BuildConfiguration::Debug;
        m_settings->save();
    });

    toolBar->addSeparator();
    toolBar->addAction(action("debug"));
    toolBar->addAction(action("stop"));
    toolBar->addAction(action("stepOver"));
    toolBar->addAction(action("stepInto"));
    toolBar->addAction(action("stepOut"));

    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(spacer);
    toolBar->addAction(action("toggleSidebar"));
    toolBar->addAction(action("togglePanel"));
}

void MainWindow::createCentralArea()
{
    auto *editorArea = new QWidget;
    auto *editorLayout = new QVBoxLayout(editorArea);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);
    editorLayout->addWidget(m_editors, 1);
    editorLayout->addWidget(m_findBar);

    m_centralStack->addWidget(m_welcome);
    m_centralStack->addWidget(editorArea);

    m_verticalSplitter->addWidget(m_centralStack);
    m_verticalSplitter->addWidget(m_output);
    m_verticalSplitter->setStretchFactor(0, 1);
    m_verticalSplitter->setStretchFactor(1, 0);
    m_verticalSplitter->setSizes({520, 200});
    m_verticalSplitter->setChildrenCollapsible(false);

    m_horizontalSplitter->addWidget(m_sidebar);
    m_horizontalSplitter->addWidget(m_verticalSplitter);
    m_horizontalSplitter->setStretchFactor(0, 0);
    m_horizontalSplitter->setStretchFactor(1, 1);
    m_horizontalSplitter->setSizes({230, 950});
    m_horizontalSplitter->setChildrenCollapsible(false);
    m_sidebar->setMinimumWidth(150);

    setCentralWidget(m_horizontalSplitter);
}

void MainWindow::createStatusBar()
{
    m_cursorLabel = new QLabel;
    m_indentLabel = new QLabel;
    m_eolLabel = new QLabel;
    m_languageLabel = new QLabel;
    m_toolLabel = new QLabel(tr("Detecting compilers..."));
    m_leetCodeLabel = new QLabel;
    m_leetCodeLabel->hide();
    statusBar()->addPermanentWidget(m_leetCodeLabel);
    statusBar()->addPermanentWidget(m_cursorLabel);
    statusBar()->addPermanentWidget(m_indentLabel);
    statusBar()->addPermanentWidget(m_eolLabel);
    statusBar()->addPermanentWidget(m_languageLabel);
    statusBar()->addPermanentWidget(m_toolLabel);
    statusBar()->setSizeGripEnabled(false);
}

void MainWindow::connectComponents()
{
    connect(m_settings, &Settings::changed, this, &MainWindow::applySettings);
    connect(m_settings, &Settings::recentChanged, this, &MainWindow::rebuildRecentMenus);

    connect(m_editors, &EditorWidget::currentEditorChanged, this, [this](CodeEditor *editor) {
        m_findBar->setEditor(editor);
        showCentralPage();
        updateWindowTitle();
        updateDocumentLabels();
        updateActions();
        m_sidebar->setOpenFiles(m_editors->editors(), editor);
        refreshSymbols();
    });
    connect(m_editors, &EditorWidget::textEdited, this, [this] {
        if (m_symbolsVisible)
            m_symbolTimer.start();
    });
    connect(m_editors, &EditorWidget::documentsChanged, this, [this] {
        showCentralPage();
        updateWindowTitle();
        updateActions();
        m_sidebar->setOpenFiles(m_editors->editors(), m_editors->currentEditor());
    });
    connect(m_editors, &EditorWidget::cursorMoved, this, &MainWindow::updateCursorLabel);
    connect(m_editors, &EditorWidget::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, statusMessageMs);
    });
    connect(m_editors, &EditorWidget::fileOpened, this, [this](const QString &path) {
        if (!m_settings->store().contains("sidebar/rootFolder"))
            m_sidebar->setRootFolder(QFileInfo(path).absolutePath());
    });
    connect(m_editors, &EditorWidget::breakpointToggled, this,
            [this](const QString &path, int line, bool enabled) {
                if (!m_debugger->isActive())
                    return;
                if (enabled)
                    m_debugger->addBreakpoint({path, line});
                else
                    m_debugger->removeBreakpoint({path, line});
            });

    connect(m_welcome, &WelcomeWidget::newCppRequested, action("newCpp"), &QAction::trigger);
    connect(m_welcome, &WelcomeWidget::newCRequested, action("newC"), &QAction::trigger);
    connect(m_welcome, &WelcomeWidget::openRequested, action("open"), &QAction::trigger);
    connect(m_welcome, &WelcomeWidget::recentActivated, this,
            [this](const QString &path) { m_editors->openFile(path); });

    connect(m_sidebar, &Sidebar::fileActivated, this,
            [this](const QString &path) { openPaths({path}); });
    connect(m_sidebar, &Sidebar::editorActivated, m_editors,
            [this](CodeEditor *editor) { m_editors->setCurrentWidget(editor); });
    connect(m_sidebar, &Sidebar::symbolActivated, this, [this](int line) {
        if (CodeEditor *editor = m_editors->currentEditor())
            editor->goToPosition(line);
    });
    connect(m_sidebar, &Sidebar::symbolsVisibilityChanged, this, [this](bool visible) {
        m_symbolsVisible = visible;
        refreshSymbols();
    });
    connect(m_sidebar, &Sidebar::rootFolderChanged, this, [this](const QString &path) {
        m_settings->store().setValue("sidebar/rootFolder", path);
    });
    connect(m_sidebar, &Sidebar::newProjectFileRequested, this, &MainWindow::newProjectFile);
    connect(m_sidebar, &Sidebar::addProjectFilesRequested, this, &MainWindow::addFilesToProject);
    connect(m_sidebar, &Sidebar::newProjectFolderRequested, this, &MainWindow::newProjectFolder);
    connect(m_sidebar, &Sidebar::removeProjectFileRequested, this, &MainWindow::removeProjectFile);

    m_symbolTimer.setSingleShot(true);
    m_symbolTimer.setInterval(symbolRefreshDelayMs);
    connect(&m_symbolTimer, &QTimer::timeout, this, &MainWindow::refreshSymbols);

    connect(m_compilers, &CompilerManager::detectionFinished, this, &MainWindow::updateToolLabel);

    connect(m_build, &BuildProcess::commandStarted, m_output, &OutputPanel::appendCommand);
    connect(m_build, &BuildProcess::outputLine, m_output, &OutputPanel::appendOutputLine);
    connect(m_build, &BuildProcess::diagnosticFound, this, [this](const Diagnostic &diagnostic) {
        m_output->addDiagnostic(diagnostic);
        m_buildDiagnostics.append(diagnostic);
    });
    connect(m_build, &BuildProcess::finished, this, &MainWindow::onBuildFinished);

    connect(m_output, &OutputPanel::diagnosticActivated, this, [this](const Diagnostic &diagnostic) {
        if (diagnostic.hasLocation())
            goToLocation(diagnostic.file, diagnostic.line, diagnostic.column);
    });

    TerminalView *terminal = m_output->terminal();
    connect(terminal, &TerminalView::inputReady, this, [this](const QByteArray &data) {
        if (m_debuggerOwnsTerminal)
            m_debugger->writeToProgram(data);
        else
            m_runner->write(data);
    });
    connect(terminal, &TerminalView::sizeChanged, m_runner, &ProcessRunner::setWindowSize);
    connect(m_runner, &ProcessRunner::output, terminal, &TerminalView::appendTerminalData);
    connect(m_runner, &ProcessRunner::finished, this, &MainWindow::onProgramFinished);

    connect(m_debugger, &DebuggerManager::inferiorOutput, terminal, &TerminalView::appendTerminalData);
    connect(m_debugger, &DebuggerManager::stateChanged, this, [this] { updateActions(); });
    connect(m_debugger, &DebuggerManager::paused, this, &MainWindow::onDebuggerPaused);
    connect(m_debugger, &DebuggerManager::resumed, this, [this] {
        m_editors->clearExecutionLines();
        m_output->debugPanel()->clearState();
        statusBar()->showMessage(tr("Running..."));
    });
    connect(m_debugger, &DebuggerManager::framesChanged, m_output->debugPanel(), &DebugPanel::setFrames);
    connect(m_debugger, &DebuggerManager::variablesChanged, m_output->debugPanel(), &DebugPanel::setVariables);
    connect(m_debugger, &DebuggerManager::consoleOutput, m_output->debugPanel(), &DebugPanel::appendConsole);
    connect(m_debugger, &DebuggerManager::frameSelected, this,
            [this](const QString &file, int line) {
                if (!file.isEmpty())
                    goToLocation(file, line, 0);
            });
    connect(m_debugger, &DebuggerManager::programExited, this, [this](int exitCode) {
        const ThemeColors &colors = Theme::colors(m_settings->appearance.theme);
        m_output->terminal()->appendStatus(tr("Process exited with code %1").arg(exitCode),
                                           exitCode == 0 ? colors.success : colors.warning);
    });
    connect(m_debugger, &DebuggerManager::errorOccurred, this, [this](const QString &message) {
        m_output->debugPanel()->appendConsole(message + QLatin1Char('\n'));
        showPanel(OutputPanel::DebugTab);
        statusBar()->showMessage(message, statusMessageMs);
    });
    connect(m_debugger, &DebuggerManager::sessionEnded, this, &MainWindow::onDebugSessionEnded);

    LeetCodePanel *leetCode = m_output->leetCodePanel();
    connect(leetCode, &LeetCodePanel::fetchRequested, this, &MainWindow::promptFetchProblem);
    connect(leetCode, &LeetCodePanel::problemActivated, this, &MainWindow::openProblemFolder);
    connect(leetCode, &LeetCodePanel::testActivated, this, &MainWindow::goToTest);
    connect(m_output, &QTabWidget::currentChanged, this, [this, leetCode](int index) {
        if (index == OutputPanel::LeetCodeTab)
            leetCode->refreshProblems();
    });
    connect(m_leetCodeClient, &LeetCodeClient::problemFetched, this, &MainWindow::onProblemFetched);
    connect(m_leetCodeClient, &LeetCodeClient::fetchFailed, this, [this](const QString &message) {
        m_refreshDirectory.clear();
        statusBar()->showMessage(tr("Could not fetch the problem"), statusMessageMs);
        QMessageBox::warning(this, tr("Could Not Fetch Problem"), message);
    });
    connect(m_testRunner, &TestSuiteRunner::testFinished, leetCode, &LeetCodePanel::addResult);
    connect(m_testRunner, &TestSuiteRunner::finished, this, &MainWindow::onTestsFinished);

    connect(m_output->debugPanel(), &DebugPanel::frameActivated, m_debugger, &DebuggerManager::selectFrame);
    connect(m_output->debugPanel(), &DebugPanel::commandEntered, this, [this](const QString &command) {
        m_output->debugPanel()->appendConsole(QStringLiteral("(gdb) ") + command + QLatin1Char('\n'));
        if (m_debugger->isActive())
            m_debugger->runConsoleCommand(command);
    });
}

// ---------------------------------------------------------------------------
// State reflected into the UI
// ---------------------------------------------------------------------------

void MainWindow::applySettings()
{
    const AppearanceSettings &appearance = m_settings->appearance;
    Theme::apply(*qApp, appearance.theme);
    applyIconColors();
    m_output->setTheme(appearance.theme);
    m_output->leetCodePanel()->setProblemsDirectory(m_settings->leetcode.directory());
    m_editors->applySettings();

    const QSignalBlocker sidebarBlocker(action("toggleSidebar"));
    const QSignalBlocker panelBlocker(action("togglePanel"));
    const QSignalBlocker wrapBlocker(action("toggleWrap"));
    const QSignalBlocker darkBlocker(action("toggleDark"));
    action("toggleSidebar")->setChecked(appearance.sidebarVisible);
    action("togglePanel")->setChecked(appearance.bottomPanelVisible);
    action("toggleWrap")->setChecked(m_settings->editor.wordWrap);
    action("toggleDark")->setChecked(appearance.theme == ThemeKind::Dark);
    m_sidebar->setVisible(appearance.sidebarVisible);
    m_output->setVisible(appearance.bottomPanelVisible);

    const QSignalBlocker configurationBlocker(m_configurationBox);
    m_configurationBox->setCurrentIndex(
        m_settings->compiler.configuration == BuildConfiguration::Release ? 1 : 0);

    updateDocumentLabels();
    detectToolsIfChanged();
}

void MainWindow::detectToolsIfChanged()
{
    const CompilerSettings &compiler = m_settings->compiler;
    const QStringList paths = {compiler.gccPath, compiler.gxxPath, compiler.gdbPath};
    if (paths == m_detectedToolPaths)
        return;
    m_detectedToolPaths = paths;
    m_compilers->detect(compiler);
}

void MainWindow::applyIconColors()
{
    const ThemeColors &colors = Theme::colors(m_settings->appearance.theme);
    for (auto it = m_actionIcons.cbegin(); it != m_actionIcons.cend(); ++it) {
        QColor color = colors.text;
        if (it.value() == QLatin1String("run") || it.value() == QLatin1String("compile-run"))
            color = colors.success;
        else if (it.value() == QLatin1String("stop"))
            color = colors.error;
        else if (it.value() == QLatin1String("debug") || it.value() == QLatin1String("continue"))
            color = colors.warning;
        m_actions.value(it.key())->setIcon(Theme::icon(it.value(), color));
    }
}

void MainWindow::updateActions()
{
    const bool hasEditor = m_editors->currentEditor() != nullptr;
    const bool building = m_build->isRunning();
    const bool running = m_runner->isRunning();
    const DebuggerManager::State state = m_debugger->state();
    const bool debugging = m_debugger->isActive();
    const bool paused = state == DebuggerManager::State::Paused;
    const bool canBuild = (hasEditor || m_project) && !building;

    for (const char *name : {"save", "saveAs", "closeTab", "undo", "redo", "cut", "copy", "paste",
                             "selectAll", "toggleComment", "find", "replace", "findNext",
                             "findPrevious", "goToLine", "goToDefinition", "toggleBreakpoint"}) {
        action(QLatin1String(name))->setEnabled(hasEditor);
    }
    action("saveAll")->setEnabled(hasEditor);
    action("closeAll")->setEnabled(hasEditor);

    action("compile")->setEnabled(canBuild);
    action("run")->setEnabled(canBuild);
    action("compileRun")->setEnabled(canBuild);
    action("build")->setEnabled(canBuild);
    action("rebuild")->setEnabled(canBuild);
    action("clean")->setEnabled(canBuild);
    action("runOptions")->setEnabled(hasEditor || m_project);
    action("stop")->setEnabled(building || running || debugging || m_testRunner->isRunning());

    action("debug")->setEnabled(paused || (!debugging && canBuild));
    action("debug")->setText(paused ? tr("&Continue") : tr("Start &Debugging"));
    const QString debugIcon = paused ? QStringLiteral("continue") : QStringLiteral("debug");
    if (m_actionIcons.value(QStringLiteral("debug")) != debugIcon) {
        m_actionIcons.insert(QStringLiteral("debug"), debugIcon);
        applyIconColors();
    }

    for (const char *name : {"stepOver", "stepInto", "stepOut"}) {
        action(QLatin1String(name))->setEnabled(paused);
        action(QLatin1String(name))->setVisible(debugging);
    }

    const bool hasProject = m_project != nullptr;
    for (const char *name : {"closeProject", "addFiles", "newProjectFile", "projectOptions"})
        action(QLatin1String(name))->setEnabled(hasProject);
}

void MainWindow::updateWindowTitle()
{
    QString title;
    if (CodeEditor *editor = m_editors->currentEditor()) {
        title = editor->displayName();
        if (editor->isModified())
            title.prepend(QStringLiteral("● "));
        title += QStringLiteral(" — ");
    }
    if (m_project)
        title += m_project->name + QStringLiteral(" — ");
    setWindowTitle(title + QStringLiteral("LightIDE"));
}

void MainWindow::updateCursorLabel(int line, int column)
{
    m_cursorLabel->setText(tr("Ln %1, Col %2").arg(line).arg(column));
}

void MainWindow::updateDocumentLabels()
{
    CodeEditor *editor = m_editors->currentEditor();
    m_cursorLabel->setVisible(editor);
    m_eolLabel->setVisible(editor);
    m_indentLabel->setVisible(editor);
    m_languageLabel->setVisible(editor);
    if (!editor)
        return;

    m_eolLabel->setText(eolName(editor->lineEnding()));
    m_indentLabel->setText((editor->indentationsUseTabs() ? tr("Tab Size: %1") : tr("Spaces: %1"))
                               .arg(editor->tabWidth()));
    m_languageLabel->setText(CompileCommand::languageOf(editor->displayName()) == SourceLanguage::C
                                 ? QStringLiteral("C")
                                 : QStringLiteral("C++"));
}

void MainWindow::updateToolLabel()
{
    const ToolInfo &gxx = m_compilers->gxx();
    const ToolInfo &gcc = m_compilers->gcc();
    const ThemeColors &colors = Theme::colors(m_settings->appearance.theme);

    if (gxx.found && gcc.found) {
        m_toolLabel->setStyleSheet({});
        m_toolLabel->setText(QStringLiteral("g++ %1").arg(gxx.version));
    } else {
        m_toolLabel->setStyleSheet(QStringLiteral("color: %1;").arg(colors.error.name()));
        m_toolLabel->setText(tr("Compiler missing"));
        m_output->appendBuildStatus(
            tr("GCC or G++ was not found. Install them with:\n%1\nOr set their paths under "
               "Tools > Settings > Compiler.").arg(CompilerManager::installHint()), false);
        showPanel(OutputPanel::CompilerTab);
    }
    m_toolLabel->setToolTip(tr("GCC %1, G++ %2, GDB %3")
                                .arg(gcc.found ? gcc.version : tr("not found"),
                                     gxx.found ? gxx.version : tr("not found"),
                                     m_compilers->gdb().found ? m_compilers->gdb().version
                                                              : tr("not found")));
}

void MainWindow::rebuildRecentMenus()
{
    const auto fill = [this](QMenu *menu, const QStringList &paths, auto open) {
        menu->clear();
        for (const QString &path : paths)
            menu->addAction(path, this, [path, open] { open(path); });
        menu->setEnabled(!paths.isEmpty());
        if (!paths.isEmpty()) {
            menu->addSeparator();
            menu->addAction(tr("Clear Recent"), m_settings, &Settings::clearRecent);
        }
    };

    fill(m_recentFilesMenu, m_settings->recentFiles(),
         [this](const QString &path) { m_editors->openFile(path); });
    fill(m_recentProjectsMenu, m_settings->recentProjects(),
         [this](const QString &path) { openProject(path); });
    m_welcome->setRecentFiles(m_settings->recentFiles());
}

void MainWindow::showCentralPage()
{
    m_centralStack->setCurrentIndex(m_editors->count() > 0 ? 1 : 0);
}

void MainWindow::refreshSymbols()
{
    if (!m_symbolsVisible)
        return;
    CodeEditor *editor = m_editors->currentEditor();
    m_sidebar->setSymbols(editor ? SymbolScanner::scan(editor->text()) : QList<Symbol>());
}

void MainWindow::showPanel(int tab)
{
    if (!m_output->isVisible())
        action("togglePanel")->setChecked(true);
    m_output->setCurrentIndex(tab);
}

void MainWindow::setPanelVisible(bool visible)
{
    m_output->setVisible(visible);
    m_settings->appearance.bottomPanelVisible = visible;
}

void MainWindow::setSidebarVisible(bool visible)
{
    m_sidebar->setVisible(visible);
    m_settings->appearance.sidebarVisible = visible;
}

// ---------------------------------------------------------------------------
// Files and projects
// ---------------------------------------------------------------------------

void MainWindow::openPaths(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (path.endsWith(QStringLiteral(".") + QLatin1String(Project::extension)))
            openProject(path);
        else
            m_editors->openFile(path);
    }
}

void MainWindow::openFileDialog()
{
    CodeEditor *editor = m_editors->currentEditor();
    const QString directory = editor && !editor->isUntitled()
                                  ? QFileInfo(editor->filePath()).absolutePath()
                                  : m_sidebar->rootFolder();
    openPaths(QFileDialog::getOpenFileNames(this, tr("Open File"), directory,
                                            FileManager::openFileFilter()));
}

void MainWindow::setProject(std::unique_ptr<Project> project)
{
    m_project = std::move(project);
    m_sidebar->setProject(m_project.get());
    if (m_project) {
        m_sidebar->setRootFolder(m_project->directory());
        m_settings->addRecentProject(m_project->filePath());
    }
    updateActions();
    updateWindowTitle();
}

void MainWindow::saveProject()
{
    if (!m_project)
        return;
    QString error;
    if (!m_project->save(&error))
        QMessageBox::critical(this, tr("Could Not Save Project"), error);
    m_sidebar->setProject(m_project.get());
}

bool MainWindow::openProject(const QString &path)
{
    auto project = std::make_unique<Project>();
    QString error;
    if (!project->load(path, &error)) {
        QMessageBox::warning(this, tr("Could Not Open Project"),
                             tr("\"%1\" could not be opened.\n\n%2").arg(path, error));
        return false;
    }
    setProject(std::move(project));
    return true;
}

void MainWindow::newProject()
{
    const QString defaultLocation =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    NewProjectDialog dialog(defaultLocation, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString directory = dialog.projectDirectory();
    if (!QDir().mkpath(directory)) {
        QMessageBox::critical(this, tr("Could Not Create Project"),
                              tr("The folder \"%1\" could not be created.").arg(directory));
        return;
    }

    auto project = std::make_unique<Project>(
        Project::create(directory, dialog.projectName(), dialog.isCpp()));
    const QString mainFile = project->absolutePath(project->sources.first());
    QString error;
    if (!QFileInfo::exists(mainFile)) {
        const QString contents = dialog.isCpp() ? FileManager::cppTemplate() : FileManager::cTemplate();
        if (!FileManager::save(mainFile, contents, &error)) {
            QMessageBox::critical(this, tr("Could Not Create Project"), error);
            return;
        }
    }
    if (!project->save(&error)) {
        QMessageBox::critical(this, tr("Could Not Create Project"), error);
        return;
    }
    setProject(std::move(project));
    m_editors->openFile(mainFile);
}

void MainWindow::openProjectDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), m_sidebar->rootFolder(),
        tr("LightIDE projects (*.%1)").arg(QLatin1String(Project::extension)));
    if (!path.isEmpty())
        openProject(path);
}

void MainWindow::closeProject()
{
    setProject(nullptr);
}

void MainWindow::addFilesToProject()
{
    if (!m_project)
        return;
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Files"), m_project->directory(), FileManager::openFileFilter());
    for (const QString &path : paths)
        m_project->addFile(path);
    if (!paths.isEmpty())
        saveProject();
}

void MainWindow::newProjectFile(const QString &folder)
{
    if (!m_project)
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("New File"), tr("File name, with a supported extension such as .cpp or .h:"),
        QLineEdit::Normal, QString(), &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;

    const QString relative = folder.isEmpty() ? name.trimmed() : folder + QLatin1Char('/') + name.trimmed();
    const QString path = m_project->absolutePath(relative);
    if (!FileManager::isSupportedFile(path)) {
        QMessageBox::warning(this, tr("New File"),
                             tr("Use one of: %1").arg(FileManager::supportedPatterns().join(QStringLiteral(", "))));
        return;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QString error;
    if (!QFileInfo::exists(path) && !FileManager::save(path, QString(), &error)) {
        QMessageBox::critical(this, tr("New File"), error);
        return;
    }
    m_project->addFile(path);
    saveProject();
    m_editors->openFile(path);
}

void MainWindow::newProjectFolder(const QString &parentFolder)
{
    if (!m_project)
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, QString(), &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;

    const QString relative =
        parentFolder.isEmpty() ? name.trimmed() : parentFolder + QLatin1Char('/') + name.trimmed();
    QDir().mkpath(m_project->absolutePath(relative));
    if (!m_project->folders.contains(relative))
        m_project->folders.append(relative);
    saveProject();
}

void MainWindow::removeProjectFile(const QString &path)
{
    // The file stays on disk; this only takes it out of the project.
    if (m_project && m_project->removeFile(path))
        saveProject();
}

void MainWindow::showProjectOptions()
{
    if (!m_project)
        return;
    ProjectOptionsDialog dialog(*m_project, this);
    if (dialog.exec() == QDialog::Accepted) {
        dialog.applyTo(*m_project);
        saveProject();
    }
}

// ---------------------------------------------------------------------------
// Build, run and debug
// ---------------------------------------------------------------------------

bool MainWindow::usesProject() const
{
    if (!m_project)
        return false;
    CodeEditor *editor = m_editors->currentEditor();
    return !editor || editor->isUntitled() || m_project->contains(editor->filePath());
}

void MainWindow::whenIdle(std::function<void()> action)
{
    // Rebuilding while the old executable runs would fail with "Text file busy",
    // so whatever is running stops first and the request continues afterwards.
    if (m_runner->isRunning()) {
        m_whenIdle = std::move(action);
        m_runner->stop();
    } else if (m_debugger->isActive()) {
        m_whenIdle = std::move(action);
        m_debugger->stop();
    } else {
        action();
    }
}

bool MainWindow::checkCompilerFor(bool needsGxx, bool needsGcc)
{
    if (m_compilers->detecting())
        return true;   // BuildProcess reports the failure if the tool turns out to be missing.
    if (needsGxx && !m_compilers->gxx().found) {
        showMissingToolMessage(QStringLiteral("G++"));
        return false;
    }
    if (needsGcc && !m_compilers->gcc().found) {
        showMissingToolMessage(QStringLiteral("GCC"));
        return false;
    }
    return true;
}

std::optional<BuildPlan> MainWindow::makeSingleFilePlan(CodeEditor *editor,
                                                        BuildConfiguration configuration)
{
    if (!m_editors->save(editor))
        return std::nullopt;
    // Headers open in other tabs are part of what gets compiled.
    for (CodeEditor *other : m_editors->editors()) {
        if (other != editor && other->isModified() && !other->isUntitled())
            m_editors->save(other);
    }

    const QString source = editor->filePath();
    if (!FileManager::isSourceFile(source)) {
        QMessageBox::information(this, tr("Nothing to Compile"),
                                 tr("\"%1\" is not a C or C++ source file.").arg(editor->displayName()));
        return std::nullopt;
    }

    const bool cpp = CompileCommand::languageOf(source) == SourceLanguage::Cpp;
    if (!checkCompilerFor(cpp, !cpp))
        return std::nullopt;

    const CompilerSettings &compiler = m_settings->compiler;
    const FileRunOptions runOptions = m_settings->runOptions(source);
    const QString output =
        CompileCommand::outputPathFor(source, compiler.outputDirectory, runOptions.outputName);

    BuildPlan plan;
    plan.compileJobs.append(BuildJob{
        CompileCommand::singleFile(compiler.toolchain(), compiler.buildOptions(configuration),
                                   source, output),
        editor->displayName()});
    plan.workingDirectory = QFileInfo(source).absolutePath();
    plan.outputPath = output;
    plan.directories << QFileInfo(output).absolutePath();
    return plan;
}

void MainWindow::startPlan(const BuildPlan &plan, AfterBuild after)
{
    m_afterBuild = after;
    m_buildDiagnostics.clear();
    m_editors->clearDiagnostics();
    m_output->clearBuild();
    showPanel(OutputPanel::CompilerTab);
    statusBar()->showMessage(tr("Building..."));
    m_build->start(plan);
    updateActions();
}

void MainWindow::startBuild(AfterBuild after, BuildConfiguration configuration)
{
    if (m_build->isRunning())
        return;

    std::optional<BuildPlan> plan;
    if (usesProject()) {
        if (!m_editors->saveAll())
            return;
        if (!checkCompilerFor(true, false))
            return;
        plan = m_project->makePlan(m_settings->compiler.toolchain(), configuration);
    } else {
        CodeEditor *editor = m_editors->currentEditor();
        if (!editor)
            return;
        plan = makeSingleFilePlan(editor, configuration);
    }
    if (plan)
        startPlan(*plan, after);
}

void MainWindow::onBuildFinished(const BuildResult &result)
{
    m_editors->applyDiagnostics(m_buildDiagnostics);

    const QString counts = tr("%n error(s), ", nullptr, result.errors)
                           + tr("%n warning(s)", nullptr, result.warnings);
    QString summary;
    if (result.cancelled) {
        summary = tr("Build cancelled after %1 s").arg(formatSeconds(result.durationMs));
    } else if (!result.failureMessage.isEmpty()) {
        summary = result.failureMessage + QLatin1Char('\n') + CompilerManager::installHint();
    } else if (result.upToDate) {
        summary = tr("Everything is up to date.");
    } else if (result.success) {
        summary = tr("Build succeeded in %1 s: %2 (exit code %3)")
                      .arg(formatSeconds(result.durationMs), counts)
                      .arg(result.exitCode);
    } else {
        summary = tr("Build failed in %1 s: %2 (exit code %3)")
                      .arg(formatSeconds(result.durationMs), counts)
                      .arg(result.exitCode);
    }
    m_output->appendBuildStatus(summary, result.success);
    statusBar()->showMessage(result.success ? tr("Build succeeded") : tr("Build failed"),
                             statusMessageMs);

    const AfterBuild after = std::exchange(m_afterBuild, AfterBuild::Nothing);
    updateActions();

    if (!result.success) {
        const bool hasMessages = !m_output->diagnostics().isEmpty();
        showPanel(hasMessages ? OutputPanel::MessagesTab : OutputPanel::CompilerTab);
    } else if (result.warnings > 0 && after == AfterBuild::Nothing) {
        showPanel(OutputPanel::MessagesTab);
    }
    emit buildReported(result);

    if (!result.success) {
        if (after == AfterBuild::LeetCodeTests) {
            m_output->leetCodePanel()->showStatus(tr("The solution did not compile. See Build Messages."));
            m_activeProblem.reset();
        }
        return;
    }

    switch (after) {
    case AfterBuild::Run:
        if (const auto target = currentRunTarget())
            launch(*target);
        break;
    case AfterBuild::Debug:
        beginDebugSession();
        break;
    case AfterBuild::LeetCodeTests: {
        const ProblemFolder folder = *m_activeProblem;
        const LeetCodeRunSettings settings = leetCodeRunSettings();
        if (!result.upToDate)
            folder.markBuilt(settings, false);
        const ProblemMetadata metadata = folder.readMetadata();
        showPanel(OutputPanel::LeetCodeTab);
        m_output->leetCodePanel()->showStatus(tr("Running tests..."));
        m_testRunner->start(folder, metadata, folder.readTests(metadata), settings, m_onlyTests);
        updateActions();
        break;
    }
    case AfterBuild::LeetCodeDebug: {
        const ProblemFolder folder = *m_activeProblem;
        if (!result.upToDate)
            folder.markBuilt(leetCodeRunSettings(), true);
        beginDebugSession(RunTarget{folder.binaryPath(true), {}, {}, folder.directory()});
        break;
    }
    case AfterBuild::Nothing:
        break;
    }
}

void MainWindow::compile()
{
    whenIdle([this] { startBuild(AfterBuild::Nothing, m_settings->compiler.configuration); });
}

void MainWindow::compileAndRun()
{
    whenIdle([this] { startBuild(AfterBuild::Run, m_settings->compiler.configuration); });
}

void MainWindow::buildProject()
{
    compile();
}

void MainWindow::rebuildAll()
{
    whenIdle([this] {
        if (usesProject()) {
            QDir(m_project->buildDirectory(m_settings->compiler.configuration)).removeRecursively();
        }
        startBuild(AfterBuild::Nothing, m_settings->compiler.configuration);
    });
}

void MainWindow::clean()
{
    whenIdle([this] {
        if (usesProject()) {
            QDir(m_project->buildDirectory(m_settings->compiler.configuration)).removeRecursively();
        } else if (const auto target = currentRunTarget()) {
            QFile::remove(target->program);
        }
        m_output->clearBuild();
        m_editors->clearDiagnostics();
        statusBar()->showMessage(tr("Build output removed"), statusMessageMs);
    });
}

std::optional<MainWindow::RunTarget> MainWindow::currentRunTarget()
{
    const BuildConfiguration configuration = m_settings->compiler.configuration;
    RunTarget target;

    if (usesProject()) {
        if (m_project->targetType != TargetType::Executable) {
            QMessageBox::information(this, tr("Nothing to Run"),
                                     tr("This project builds a library, which cannot be run."));
            return std::nullopt;
        }
        target.program = m_project->outputPath(configuration);
        target.argumentText = m_project->runArguments;
        target.workingDirectory = m_project->runWorkingDirectory();
    } else {
        CodeEditor *editor = m_editors->currentEditor();
        if (!editor || editor->isUntitled())
            return std::nullopt;
        const FileRunOptions options = m_settings->runOptions(editor->filePath());
        target.program = CompileCommand::outputPathFor(
            editor->filePath(), m_settings->compiler.outputDirectory, options.outputName);
        target.argumentText = options.arguments;
        target.workingDirectory = options.workingDirectory;
    }
    target.arguments = CompileCommand::splitArguments(target.argumentText);
    return target;
}

bool MainWindow::runTargetIsStale(const RunTarget &target)
{
    const QFileInfo program(target.program);
    if (!program.exists())
        return true;

    if (usesProject()) {
        for (CodeEditor *editor : m_editors->editors()) {
            if (editor->isModified())
                return true;
        }
        const BuildPlan plan =
            m_project->makePlan(m_settings->compiler.toolchain(), m_settings->compiler.configuration);
        return !plan.compileJobs.isEmpty() || plan.linkJob.has_value();
    }

    CodeEditor *editor = m_editors->currentEditor();
    return !editor || editor->isModified()
           || QFileInfo(editor->filePath()).lastModified() > program.lastModified();
}

void MainWindow::run()
{
    whenIdle([this] {
        CodeEditor *editor = m_editors->currentEditor();
        if (!usesProject() && (!editor || editor->isUntitled())) {
            startBuild(AfterBuild::Run, m_settings->compiler.configuration);
            return;
        }
        const auto target = currentRunTarget();
        if (!target)
            return;
        // Run always means the code on screen, so a stale binary is rebuilt first.
        if (runTargetIsStale(*target))
            startBuild(AfterBuild::Run, m_settings->compiler.configuration);
        else
            launch(*target);
    });
}

void MainWindow::launch(const RunTarget &target)
{
    const ThemeColors &colors = Theme::colors(m_settings->appearance.theme);
    TerminalView *terminal = m_output->terminal();
    terminal->clearAll();
    showPanel(OutputPanel::ApplicationTab);

    QString error;
    m_debuggerOwnsTerminal = false;
    const RunRequest request{target.program, target.arguments, target.workingDirectory};
    if (!m_runner->start(request, &error)) {
        terminal->appendStatus(error, colors.error);
        statusBar()->showMessage(error, statusMessageMs);
        return;
    }

    terminal->appendStatus(target.argumentText.isEmpty()
                               ? target.program
                               : target.program + QLatin1Char(' ') + target.argumentText,
                           colors.textMuted);
    terminal->setInputEnabled(true);
    statusBar()->showMessage(tr("Running..."));
    updateActions();
}

void MainWindow::onProgramFinished(const RunResult &result)
{
    const ThemeColors &colors = Theme::colors(m_settings->appearance.theme);
    QColor color = colors.success;
    if (result.crashed())
        color = colors.error;
    else if (result.stoppedByUser || result.exitCode != 0)
        color = colors.warning;

    TerminalView *terminal = m_output->terminal();
    terminal->setInputEnabled(false);
    terminal->appendStatus(result.description(), color);
    statusBar()->showMessage(result.description(), statusMessageMs);
    updateActions();

    if (m_whenIdle)
        std::exchange(m_whenIdle, nullptr)();
}

QList<SourceLocation> MainWindow::collectBreakpoints() const
{
    QList<SourceLocation> breakpoints;
    for (CodeEditor *editor : m_editors->editors()) {
        if (editor->isUntitled())
            continue;
        const QList<int> lines = editor->breakpointLines();
        for (const int line : lines)
            breakpoints.append({editor->filePath(), line});
    }
    return breakpoints;
}

void MainWindow::startOrContinueDebugging()
{
    if (m_debugger->state() == DebuggerManager::State::Paused) {
        m_debugger->continueExecution();
        return;
    }
    if (m_debugger->isActive())
        return;

    if (!m_compilers->gdb().found && !m_compilers->detecting()) {
        showMissingToolMessage(QStringLiteral("GDB"));
        return;
    }
    // Debugging needs symbols whatever configuration is selected.
    whenIdle([this] { startBuild(AfterBuild::Debug, BuildConfiguration::Debug); });
}

void MainWindow::beginDebugSession()
{
    // Debug builds land in the Debug directory even while Release is selected.
    const BuildConfiguration selected = m_settings->compiler.configuration;
    m_settings->compiler.configuration = BuildConfiguration::Debug;
    const auto target = currentRunTarget();
    m_settings->compiler.configuration = selected;
    if (target)
        beginDebugSession(*target);
}

void MainWindow::beginDebugSession(const RunTarget &target)
{
    TerminalView *terminal = m_output->terminal();
    terminal->clearAll();
    terminal->appendStatus(tr("Debugging %1").arg(target.program),
                           Theme::colors(m_settings->appearance.theme).textMuted);
    m_output->debugPanel()->clearAll();

    m_debuggerOwnsTerminal = true;
    terminal->setInputEnabled(true);
    showPanel(OutputPanel::DebugTab);

    DebugRequest request;
    request.gdbPath = m_settings->compiler.gdbPath;
    request.program = target.program;
    request.arguments = target.argumentText;
    request.workingDirectory = target.workingDirectory.isEmpty()
                                   ? QFileInfo(target.program).absolutePath()
                                   : target.workingDirectory;
    request.breakpoints = collectBreakpoints();
    m_debugger->start(request);
    updateActions();
}

void MainWindow::onDebuggerPaused(const QString &file, int line, const QString &reason)
{
    m_editors->clearExecutionLines();
    showPanel(OutputPanel::DebugTab);
    if (!file.isEmpty() && line > 0) {
        if (CodeEditor *editor = m_editors->openFile(file))
            editor->setExecutionLine(line);
        statusBar()->showMessage(tr("%1 at %2:%3").arg(reason, QFileInfo(file).fileName()).arg(line));
    } else {
        statusBar()->showMessage(tr("%1, in code without debug information. Step Out returns to "
                                    "your program.").arg(reason));
    }
    updateActions();
}

void MainWindow::onDebugSessionEnded()
{
    m_debuggerOwnsTerminal = false;
    m_output->terminal()->setInputEnabled(false);
    m_output->debugPanel()->clearState();
    m_editors->clearExecutionLines();
    statusBar()->showMessage(tr("Debug session ended"), statusMessageMs);
    updateActions();

    if (m_whenIdle)
        std::exchange(m_whenIdle, nullptr)();
}

void MainWindow::stop()
{
    m_whenIdle = nullptr;
    m_afterBuild = AfterBuild::Nothing;
    if (m_build->isRunning())
        m_build->cancel();
    if (m_debugger->isActive())
        m_debugger->stop();
    if (m_runner->isRunning())
        m_runner->stop();
    if (m_testRunner->isRunning())
        m_testRunner->cancel();
}

void MainWindow::showRunOptions()
{
    if (usesProject()) {
        showProjectOptions();
        return;
    }
    CodeEditor *editor = m_editors->currentEditor();
    if (!editor)
        return;
    if (editor->isUntitled() && !m_editors->save(editor))
        return;

    RunOptionsDialog dialog(m_settings->runOptions(editor->filePath()), this);
    if (dialog.exec() == QDialog::Accepted)
        m_settings->setRunOptions(editor->filePath(), dialog.options());
}

void MainWindow::showMissingToolMessage(const QString &toolName)
{
    QMessageBox box(this);
    box.setObjectName(QStringLiteral("missingToolMessage"));
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("%1 Not Found").arg(toolName));
    box.setText(tr("LightIDE uses the %1 installed on your system, and could not find it.").arg(toolName));
    box.setInformativeText(tr("On Ubuntu, install it with:\n\n%1\n\nIf it is installed elsewhere, "
                              "set its path under Tools > Settings > Compiler.")
                               .arg(CompilerManager::installHint()));
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.exec();
}

void MainWindow::showCompilerStatus()
{
    const auto row = [](const ToolInfo &tool) {
        return QStringLiteral("<tr><td><b>%1</b>&nbsp;&nbsp;</td><td>%2</td><td>&nbsp;&nbsp;%3</td></tr>")
            .arg(tool.name, tool.found ? tool.version : tr("not found"), tool.path);
    };

    QString text = QStringLiteral("<table>") + row(m_compilers->gcc()) + row(m_compilers->gxx())
                   + row(m_compilers->gdb()) + QStringLiteral("</table>");
    if (!m_compilers->gcc().found || !m_compilers->gxx().found || !m_compilers->gdb().found) {
        text += QStringLiteral("<p>%1</p><pre>%2</pre>")
                    .arg(tr("To install the missing tools on Ubuntu:"),
                         CompilerManager::installHint().toHtmlEscaped());
    }
    QMessageBox::information(this, tr("Compiler Status"), text);
}

// ---------------------------------------------------------------------------
// LeetCode
// ---------------------------------------------------------------------------

LeetCodeRunSettings MainWindow::leetCodeRunSettings() const
{
    LeetCodeRunSettings settings;
    settings.compiler = m_settings->compiler.gxxPath;
    settings.standard = m_settings->leetcode.standard;
    settings.flags = CompileCommand::splitArguments(m_settings->leetcode.flags);
    settings.sanitizers = m_settings->leetcode.sanitizers;
    settings.timeoutMs = m_settings->leetcode.timeoutMs;
    return settings;
}

std::optional<ProblemFolder> MainWindow::currentProblem()
{
    if (CodeEditor *editor = m_editors->currentEditor()) {
        const QString directory = ProblemFolder::folderOfSolution(editor->filePath());
        if (!directory.isEmpty())
            return ProblemFolder(directory);
    }
    QMessageBox::information(this, tr("No Problem Open"),
                             tr("Open a problem's solution.cpp first, or fetch one with LeetCode > Fetch Problem."));
    return std::nullopt;
}

void MainWindow::promptFetchProblem()
{
    // A problem link on the clipboard is almost always the one to fetch.
    const QString clipboard = QApplication::clipboard()->text().trimmed();
    const QString suggestion = clipboard.contains(QLatin1String("leetcode.")) ? clipboard : QString();

    bool accepted = false;
    const QString reference = QInputDialog::getText(
        this, tr("Fetch Problem"), tr("Problem URL, slug or title:"), QLineEdit::Normal, suggestion, &accepted);
    if (accepted)
        fetchProblem(reference);
}

void MainWindow::fetchProblem(const QString &reference)
{
    try {
        const QString slug = QuestionParser::parseProblemReference(reference);
        statusBar()->showMessage(tr("Fetching %1...").arg(slug));
        m_leetCodeClient->fetchProblem(slug);
    } catch (const LcError &error) {
        QMessageBox::warning(this, tr("Fetch Problem"), error.message());
    }
}

void MainWindow::onProblemFetched(const LeetCodeProblem &problem)
{
    try {
        if (!m_refreshDirectory.isEmpty()) {
            const ProblemFolder folder(std::exchange(m_refreshDirectory, QString()));
            const bool signatureChanged = folder.refresh(problem);
            m_output->leetCodePanel()->showProblem(folder);
            statusBar()->showMessage(tr("Refreshed %1").arg(problem.title), statusMessageMs);
            if (signatureChanged) {
                QMessageBox::information(this, tr("Signature Changed"),
                                         tr("LeetCode changed this problem's method signature. Your "
                                            "solution.cpp was not touched, so adapt it to the new "
                                            "starter code:\n\n%1").arg(problem.starterCode));
            }
            return;
        }

        const QString directory = QDir(m_settings->leetcode.directory()).filePath(problem.slug);
        const ProblemFolder folder(directory);
        const QStringList warnings = folder.create(problem, false);

        if (CodeEditor *editor = m_editors->openFile(folder.solutionPath())) {
            Q_UNUSED(editor)
            LeetCodePanel *panel = m_output->leetCodePanel();
            panel->setProblemsDirectory(m_settings->leetcode.directory());
            panel->showProblem(folder);
            panel->showStatus(warnings.isEmpty()
                                  ? tr("%1 is ready. Write your solution, then press Ctrl+Shift+B.").arg(problem.title)
                                  : warnings.join(QLatin1Char('\n')));
            showPanel(OutputPanel::LeetCodeTab);
        }
        statusBar()->showMessage(tr("Fetched %1").arg(problem.title), statusMessageMs);
    } catch (const LcError &error) {
        QMessageBox::warning(this, tr("Could Not Create Problem"), error.message());
    }
}

// The harness is rewritten inside the editor, so that the undo history survives
// and the file on disk never disagrees with the tab.
CodeEditor *MainWindow::syncProblemEditor(const ProblemFolder &folder, const ProblemMetadata &metadata,
                                          const QList<LcTestCase> &tests)
{
    CodeEditor *editor = m_editors->openFile(folder.solutionPath());
    if (!editor)
        return nullptr;

    const QString current = editor->text();
    const QString updated = LeetCodeBuilder::syncHarness(current, metadata, tests);
    if (updated != current) {
        qsizetype common = 0;
        const qsizetype limit = std::min(current.size(), updated.size());
        while (common < limit && current.at(common) == updated.at(common))
            ++common;
        const qsizetype lineStart = current.lastIndexOf(u'\n', common - 1) + 1;
        const int line = static_cast<int>(current.left(lineStart).count(u'\n'));

        int cursorLine, cursorIndex;
        editor->getCursorPosition(&cursorLine, &cursorIndex);
        const int lastLine = editor->lines() - 1;
        editor->setSelection(line, 0, lastLine, editor->lineLength(lastLine));
        editor->replaceSelectedText(updated.mid(lineStart));
        editor->setCursorPosition(cursorLine, cursorIndex);
    }
    return m_editors->save(editor) ? editor : nullptr;
}

void MainWindow::buildProblem(AfterBuild after, const QList<int> &onlyTests)
{
    const auto folder = currentProblem();
    if (!folder)
        return;

    whenIdle([this, folder = *folder, after, onlyTests] {
        if (m_build->isRunning() || !checkCompilerFor(true, false))
            return;
        try {
            const ProblemMetadata metadata = folder.readMetadata();
            const QList<LcTestCase> tests = folder.readTests(metadata);
            if (!syncProblemEditor(folder, metadata, tests))
                return;

            m_activeProblem = folder;
            m_onlyTests = onlyTests;
            LeetCodePanel *panel = m_output->leetCodePanel();
            panel->clearResults();
            panel->showProblem(folder);
            panel->showStatus(tr("Compiling..."));
            const bool debug = after == AfterBuild::LeetCodeDebug;
            startPlan(folder.makeBuildPlan(metadata, leetCodeRunSettings(), debug), after);
        } catch (const LcError &error) {
            QMessageBox::warning(this, tr("LeetCode"), error.message());
        }
    });
}

void MainWindow::runLeetCodeTests(const QList<int> &onlyTests)
{
    buildProblem(AfterBuild::LeetCodeTests, onlyTests);
}

void MainWindow::debugSolution()
{
    if (!m_compilers->gdb().found && !m_compilers->detecting()) {
        showMissingToolMessage(QStringLiteral("GDB"));
        return;
    }
    buildProblem(AfterBuild::LeetCodeDebug, {});
}

void MainWindow::runCurrentTest()
{
    const auto folder = currentProblem();
    if (!folder)
        return;

    // The test whose harness section holds the cursor.
    CodeEditor *editor = m_editors->currentEditor();
    int line, index;
    editor->getCursorPosition(&line, &index);
    static const QRegularExpression testComment(QStringLiteral(R"(^\s*// Test Case (\d+))"));
    for (int i = line; i >= 0; --i) {
        const auto match = testComment.match(editor->text(i));
        if (match.hasMatch()) {
            runLeetCodeTests({match.captured(1).toInt()});
            return;
        }
    }

    try {
        const ProblemMetadata metadata = folder->readMetadata();
        const QList<LcTestCase> tests = folder->readTests(metadata);
        QStringList names;
        for (const LcTestCase &test : tests)
            names << QStringLiteral("%1: %2").arg(test.id).arg(test.name);
        bool accepted = false;
        const QString chosen = QInputDialog::getItem(this, tr("Run Test"), tr("Test to run:"), names, 0, false, &accepted);
        if (accepted && !chosen.isEmpty())
            runLeetCodeTests({chosen.section(QLatin1Char(':'), 0, 0).toInt()});
    } catch (const LcError &error) {
        QMessageBox::warning(this, tr("LeetCode"), error.message());
    }
}

void MainWindow::onTestsFinished(const LcRunReport &report)
{
    if (!m_activeProblem)
        return;

    QString title = QFileInfo(m_activeProblem->directory()).fileName();
    try {
        title = m_activeProblem->readMetadata().title;
    } catch (const LcError &) {
        // The folder name will do.
    }

    m_output->leetCodePanel()->showReport(report, title);
    m_leetCodeLabel->setText(QStringLiteral("LC: %1 | %2 %3/%4")
                                 .arg(title, report.failed ? QString::fromUtf8("✗") : QString::fromUtf8("✓"))
                                 .arg(report.passed)
                                 .arg(report.total));
    m_leetCodeLabel->show();
    statusBar()->showMessage(tr("%1 of %2 tests passed").arg(report.passed).arg(report.total), statusMessageMs);
    updateActions();
}

void MainWindow::copySolution()
{
    CodeEditor *editor = m_editors->currentEditor();
    if (!currentProblem())
        return;
    try {
        QApplication::clipboard()->setText(SolutionFile::extractSubmission(editor->text()));
        statusBar()->showMessage(tr("Solution copied. Paste it into LeetCode and submit."), statusMessageMs);
    } catch (const LcError &error) {
        QMessageBox::warning(this, tr("Copy Solution"), error.message());
    }
}

void MainWindow::addCustomTest()
{
    const auto folder = currentProblem();
    if (!folder)
        return;
    try {
        const ProblemMetadata metadata = folder->readMetadata();
        AddTestDialog dialog(metadata, this);
        if (dialog.exec() != QDialog::Accepted)
            return;

        QList<LcTestCase> tests = folder->readTests(metadata);
        LcTestCase test = dialog.test();
        test.id = static_cast<int>(tests.size()) + 1;
        test.name = tr("Custom %1").arg(test.id);
        tests.append(test);
        folder->writeTests(metadata, tests);
        statusBar()->showMessage(tr("Added test %1").arg(test.id), statusMessageMs);
    } catch (const LcError &error) {
        QMessageBox::warning(this, tr("Add Custom Test"), error.message());
    }
}

void MainWindow::editTests()
{
    if (const auto folder = currentProblem())
        m_editors->openFile(folder->testsPath());
}

void MainWindow::refreshProblem()
{
    const auto folder = currentProblem();
    if (!folder)
        return;
    try {
        m_refreshDirectory = folder->directory();
        statusBar()->showMessage(tr("Refreshing..."));
        m_leetCodeClient->fetchProblem(folder->readMetadata().slug);
    } catch (const LcError &error) {
        m_refreshDirectory.clear();
        QMessageBox::warning(this, tr("Refresh Problem"), error.message());
    }
}

void MainWindow::openProblemInBrowser()
{
    const auto folder = currentProblem();
    if (!folder)
        return;
    try {
        QDesktopServices::openUrl(QUrl(folder->readMetadata().url));
    } catch (const LcError &error) {
        QMessageBox::warning(this, tr("LeetCode"), error.message());
    }
}

void MainWindow::connectAccount()
{
    bool accepted = false;
    const QString session = QInputDialog::getText(
        this, tr("Connect Account"),
        tr("Only needed for Premium problems. Sign in to leetcode.com in your browser and paste\n"
           "the value of its LEETCODE_SESSION cookie. It stays in memory until you close LightIDE\n"
           "and is never written to disk."),
        QLineEdit::Password, QString(), &accepted);
    if (!accepted || session.trimmed().isEmpty())
        return;
    m_leetCodeClient->setSession(session.trimmed());
    statusBar()->showMessage(tr("Connected for this session"), statusMessageMs);
}

void MainWindow::disconnectAccount()
{
    m_leetCodeClient->setSession({});
    statusBar()->showMessage(tr("Disconnected"), statusMessageMs);
}

void MainWindow::openProblemFolder(const QString &directory)
{
    const ProblemFolder folder(directory);
    m_editors->openFile(folder.solutionPath());
    m_output->leetCodePanel()->showProblem(folder);
}

void MainWindow::goToTest(int id)
{
    CodeEditor *editor = m_editors->currentEditor();
    if (!editor)
        return;
    const QRegularExpression comment(QStringLiteral(R"(^\s*// Test Case %1\b)").arg(id));
    for (int line = 0; line < editor->lines(); ++line) {
        if (comment.match(editor->text(line)).hasMatch()) {
            editor->goToPosition(line + 1);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void MainWindow::goToLocation(const QString &file, int line, int column)
{
    if (CodeEditor *editor = m_editors->openFile(file))
        editor->goToPosition(line, column);
}

void MainWindow::goToLine()
{
    CodeEditor *editor = m_editors->currentEditor();
    if (!editor)
        return;
    int currentLine, index;
    editor->getCursorPosition(&currentLine, &index);
    bool accepted = false;
    const int line = QInputDialog::getInt(this, tr("Go to Line"), tr("Line number:"), currentLine + 1,
                                          1, editor->lines(), 1, &accepted);
    if (accepted)
        editor->goToPosition(line);
}

void MainWindow::goToDefinition()
{
    CodeEditor *editor = m_editors->currentEditor();
    if (!editor)
        return;
    const QString word = editor->wordAtCursor();
    if (word.isEmpty())
        return;

    QList<CodeEditor *> open = m_editors->editors();
    open.removeOne(editor);
    open.prepend(editor);
    for (CodeEditor *candidate : std::as_const(open)) {
        const int line = SymbolScanner::findDefinition(candidate->text(), word);
        if (line > 0) {
            m_editors->setCurrentWidget(candidate);
            candidate->goToPosition(line);
            return;
        }
    }

    if (m_project) {
        for (const QString &relative : m_project->sources + m_project->headers) {
            const QString path = m_project->absolutePath(relative);
            LoadedFile file;
            QString error;
            if (!FileManager::load(path, &file, &error))
                continue;
            const int line = SymbolScanner::findDefinition(file.text, word);
            if (line > 0) {
                goToLocation(path, line, 0);
                return;
            }
        }
    }
    statusBar()->showMessage(tr("No definition found for \"%1\"").arg(word), statusMessageMs);
}

void MainWindow::showFindInFiles()
{
    if (!m_findInFiles) {
        m_findInFiles = new FindInFilesDialog(this);
        connect(m_findInFiles, &FindInFilesDialog::matchActivated, this, &MainWindow::goToLocation);
        connect(m_findInFiles, &FindInFilesDialog::fileReplaced, this, [this](const QString &path) {
            statusBar()->showMessage(tr("Replaced in %1").arg(path), statusMessageMs);
        });
    }
    m_findInFiles->setFolder(m_project ? m_project->directory() : m_sidebar->rootFolder());
    if (CodeEditor *editor = m_editors->currentEditor()) {
        if (editor->hasSelectedText())
            m_findInFiles->setSearchText(editor->selectedText());
    }
    m_findInFiles->show();
    m_findInFiles->raise();
    m_findInFiles->activateWindow();
}

void MainWindow::showShortcuts()
{
    const QStringList names = {"newCpp", "open", "save", "saveAll", "closeTab", "find", "replace",
                               "findInFiles", "goToLine", "goToDefinition", "compile", "run",
                               "compileRun", "build", "debug", "stop", "stepOver", "stepInto",
                               "stepOut", "toggleBreakpoint", "toggleSidebar", "togglePanel"};
    QString rows;
    for (const QString &name : names) {
        const QAction *entry = action(name);
        rows += QStringLiteral("<tr><td>%1&nbsp;&nbsp;&nbsp;&nbsp;</td><td><b>%2</b></td></tr>")
                    .arg(entry->text().remove(QLatin1Char('&')).remove(QStringLiteral("...")),
                         entry->shortcut().toString(QKeySequence::NativeText));
    }
    QMessageBox::information(this, tr("Keyboard Shortcuts"),
                             QStringLiteral("<table>") + rows + QStringLiteral("</table>"));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About LightIDE"),
                       tr("<b>LightIDE %1</b><br>A lightweight C/C++ IDE, and a modern "
                          "alternative to Dev-C++.<br><br>Built with Qt %2 and QScintilla.")
                           .arg(QApplication::applicationVersion(), QLatin1String(qVersion())));
}

void MainWindow::adjustFontSize(int delta)
{
    int &size = m_settings->appearance.fontSize;
    const int adjusted = std::clamp(size + delta, 6, 48);
    if (adjusted == size)
        return;
    size = adjusted;
    m_settings->save();
    m_editors->applySettings();
}

// ---------------------------------------------------------------------------
// Window events
// ---------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_editors->confirmCloseAll()) {
        event->ignore();
        return;
    }

    QSettings &store = m_settings->store();
    store.setValue("window/geometry", saveGeometry());
    store.setValue("window/horizontalSplitter", m_horizontalSplitter->saveState());
    store.setValue("window/verticalSplitter", m_verticalSplitter->saveState());
    m_settings->save();

    m_build->cancel();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    openPaths(paths);
    event->acceptProposedAction();
}
