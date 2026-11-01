#pragma once

#include "core/SymbolScanner.h"

#include <QTabWidget>

class CodeEditor;
class Project;
class QFileSystemModel;
class QListWidget;
class QPushButton;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;

class Sidebar : public QTabWidget
{
    Q_OBJECT

public:
    enum Tab { FilesTab, ProjectTab, OpenFilesTab, SymbolsTab };

    explicit Sidebar(QWidget *parent = nullptr);

    QString rootFolder() const { return m_rootFolder; }

    // chosen is false for the fallback shown when the user has picked no folder,
    // so that the button keeps offering to open one.
    void setRootFolder(const QString &path, bool chosen = true);

    void setProject(const Project *project);   // Null when no project is open.
    void setOpenFiles(const QList<CodeEditor *> &editors, CodeEditor *current);
    void setSymbols(const QList<Symbol> &symbols);

    QTreeWidget *projectTree() const { return m_projectTree; }
    QListWidget *openFilesList() const { return m_openFiles; }
    QListWidget *symbolsList() const { return m_symbols; }
    QTreeView *fileTree() const { return m_fileTree; }

signals:
    void fileActivated(const QString &path);
    void editorActivated(CodeEditor *editor);
    void symbolActivated(int line);
    void symbolsVisibilityChanged(bool visible);
    void newProjectFileRequested(const QString &folder);
    void addProjectFilesRequested();
    void newProjectFolderRequested(const QString &parentFolder);
    void removeProjectFileRequested(const QString &path);
    void openFolderRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    QWidget *createFilesPage();
    void ensureFileModel();
    void onProjectContextMenu(const QPoint &position);
    QTreeWidgetItem *folderItem(const QString &relativeFolder, QTreeWidgetItem *root);

    QString m_rootFolder;
    QFileSystemModel *m_fileModel = nullptr;
    QTreeView *m_fileTree;
    QPushButton *m_folderButton;
    QTreeWidget *m_projectTree;
    QListWidget *m_openFiles;
    QListWidget *m_symbols;
};
