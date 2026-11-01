#include "ui/Sidebar.h"

#include "editor/CodeEditor.h"
#include "project/Project.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTabBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int pathRole = Qt::UserRole;
constexpr int folderRole = Qt::UserRole + 1;
constexpr int lineRole = Qt::UserRole + 2;

QString symbolPrefix(Symbol::Kind kind)
{
    switch (kind) {
    case Symbol::Kind::Class: return QStringLiteral("class ");
    case Symbol::Kind::Struct: return QStringLiteral("struct ");
    case Symbol::Kind::Enum: return QStringLiteral("enum ");
    case Symbol::Kind::Namespace: return QStringLiteral("namespace ");
    case Symbol::Kind::Function: break;
    }
    return {};
}

} // namespace

Sidebar::Sidebar(QWidget *parent)
    : QTabWidget(parent)
    , m_fileTree(new QTreeView)
    , m_folderButton(new QPushButton)
    , m_projectTree(new QTreeWidget)
    , m_openFiles(new QListWidget)
    , m_symbols(new QListWidget)
{
    setObjectName(QStringLiteral("sidebar"));
    setDocumentMode(true);
    tabBar()->setDrawBase(false);
    tabBar()->setExpanding(true);
    setUsesScrollButtons(false);

    m_projectTree->setHeaderHidden(true);
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_projectTree, &QTreeWidget::customContextMenuRequested, this,
            &Sidebar::onProjectContextMenu);
    connect(m_projectTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const QString path = item->data(0, pathRole).toString();
        if (!path.isEmpty())
            emit fileActivated(path);
    });

    connect(m_openFiles, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        emit editorActivated(item->data(pathRole).value<CodeEditor *>());
    });

    connect(m_symbols, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        emit symbolActivated(item->data(lineRole).toInt());
    });
    connect(m_symbols, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        emit symbolActivated(item->data(lineRole).toInt());
    });

    addTab(createFilesPage(), tr("Files"));
    addTab(m_projectTree, tr("Project"));
    addTab(m_openFiles, tr("Open"));
    addTab(m_symbols, tr("Symbols"));

    connect(this, &QTabWidget::currentChanged, this, [this](int index) {
        emit symbolsVisibilityChanged(index == SymbolsTab);
        if (index == FilesTab)
            ensureFileModel();
    });

    setProject(nullptr);
}

void Sidebar::showEvent(QShowEvent *event)
{
    QTabWidget::showEvent(event);
    if (currentIndex() == FilesTab)
        ensureFileModel();
}

QWidget *Sidebar::createFilesPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_folderButton->setFlat(true);
    m_folderButton->setText(tr("Open Folder..."));
    connect(m_folderButton, &QPushButton::clicked, this, &Sidebar::openFolderRequested);

    m_fileTree->setHeaderHidden(true);
    m_fileTree->setUniformRowHeights(true);
    connect(m_fileTree, &QTreeView::activated, this, [this](const QModelIndex &index) {
        if (m_fileModel && !m_fileModel->isDir(index))
            emit fileActivated(m_fileModel->filePath(index));
    });

    layout->addWidget(m_folderButton);
    layout->addWidget(m_fileTree, 1);
    return page;
}

void Sidebar::setRootFolder(const QString &path, bool chosen)
{
    m_rootFolder = path;
    const QString name = QFileInfo(path).fileName();
    m_folderButton->setText(!chosen ? tr("Open Folder...") : name.isEmpty() ? path : name);
    m_folderButton->setToolTip(chosen ? tr("%1\nClick to open another folder").arg(path)
                                      : tr("Choose the folder to work in"));
    if (m_fileModel)
        m_fileTree->setRootIndex(m_fileModel->setRootPath(path));
}

// QFileSystemModel starts a thread and file watches, so it waits until the tab
// is actually looked at.
void Sidebar::ensureFileModel()
{
    if (m_fileModel || m_rootFolder.isEmpty())
        return;

    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fileModel->setReadOnly(true);
    m_fileTree->setModel(m_fileModel);
    for (int column = 1; column < m_fileModel->columnCount(); ++column)
        m_fileTree->hideColumn(column);
    m_fileTree->setRootIndex(m_fileModel->setRootPath(m_rootFolder));
}

QTreeWidgetItem *Sidebar::folderItem(const QString &relativeFolder, QTreeWidgetItem *root)
{
    if (relativeFolder.isEmpty() || relativeFolder == QLatin1String("."))
        return root;

    QTreeWidgetItem *parent = root;
    QString path;
    const QStringList parts = relativeFolder.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        path = path.isEmpty() ? part : path + QLatin1Char('/') + part;
        QTreeWidgetItem *child = nullptr;
        for (int i = 0; i < parent->childCount(); ++i) {
            if (parent->child(i)->data(0, folderRole).toString() == path) {
                child = parent->child(i);
                break;
            }
        }
        if (!child) {
            child = new QTreeWidgetItem(parent, {part});
            child->setData(0, folderRole, path);
        }
        parent = child;
    }
    return parent;
}

void Sidebar::setProject(const Project *project)
{
    m_projectTree->clear();
    if (!project) {
        auto *hint = new QTreeWidgetItem(m_projectTree, {tr("No project open")});
        hint->setFlags(Qt::NoItemFlags);
        return;
    }

    auto *root = new QTreeWidgetItem(m_projectTree, {project->name});
    root->setData(0, folderRole, QString());
    QFont bold = root->font(0);
    bold.setBold(true);
    root->setFont(0, bold);

    for (const QString &folder : project->folders)
        folderItem(folder, root);

    const auto addFiles = [&](const QStringList &files) {
        for (const QString &relative : files) {
            const QFileInfo info(relative);
            const QString parentPath = info.path();
            auto *item = new QTreeWidgetItem(folderItem(parentPath, root), {info.fileName()});
            item->setData(0, pathRole, project->absolutePath(relative));
            item->setToolTip(0, relative);
        }
    };
    addFiles(project->sources);
    addFiles(project->headers);

    m_projectTree->expandAll();
    setCurrentIndex(ProjectTab);
}

void Sidebar::onProjectContextMenu(const QPoint &position)
{
    QTreeWidgetItem *item = m_projectTree->itemAt(position);
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;

    const QString filePath = item->data(0, pathRole).toString();
    QString folder = item->data(0, folderRole).toString();
    if (!filePath.isEmpty() && item->parent())
        folder = item->parent()->data(0, folderRole).toString();

    QMenu menu(this);
    menu.addAction(tr("New File..."), this, [this, folder] { emit newProjectFileRequested(folder); });
    menu.addAction(tr("Add Existing Files..."), this, [this] { emit addProjectFilesRequested(); });
    menu.addAction(tr("New Folder..."), this,
                   [this, folder] { emit newProjectFolderRequested(folder); });
    if (!filePath.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Remove from Project"), this,
                       [this, filePath] { emit removeProjectFileRequested(filePath); });
    }
    menu.exec(m_projectTree->viewport()->mapToGlobal(position));
}

void Sidebar::setOpenFiles(const QList<CodeEditor *> &editors, CodeEditor *current)
{
    m_openFiles->clear();
    for (CodeEditor *editor : editors) {
        const QString name = editor->displayName();
        auto *item = new QListWidgetItem(editor->isModified() ? QStringLiteral("● ") + name : name);
        item->setData(pathRole, QVariant::fromValue(editor));
        item->setToolTip(editor->filePath());
        m_openFiles->addItem(item);
        if (editor == current)
            m_openFiles->setCurrentItem(item);
    }
}

void Sidebar::setSymbols(const QList<Symbol> &symbols)
{
    m_symbols->clear();
    for (const Symbol &symbol : symbols) {
        auto *item = new QListWidgetItem(symbolPrefix(symbol.kind) + symbol.name);
        item->setData(lineRole, symbol.line);
        item->setToolTip(tr("Line %1").arg(symbol.line));
        m_symbols->addItem(item);
    }
}
