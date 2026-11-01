#include "ui/EditorWidget.h"

#include "editor/CodeEditor.h"
#include "filesystem/FileManager.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QTimer>

namespace {

constexpr int rewatchDelayMs = 200;

QString canonical(const QString &path)
{
    const QString result = QFileInfo(path).canonicalFilePath();
    return result.isEmpty() ? QFileInfo(path).absoluteFilePath() : result;
}

} // namespace

EditorWidget::EditorWidget(Settings *settings, QWidget *parent)
    : QTabWidget(parent)
    , m_settings(settings)
    , m_watcher(new QFileSystemWatcher(this))
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    setElideMode(Qt::ElideMiddle);
    tabBar()->setDrawBase(false);
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) {
        closeEditor(qobject_cast<CodeEditor *>(widget(index)));
    });
    connect(this, &QTabWidget::currentChanged, this, [this] {
        CodeEditor *editor = currentEditor();
        emit currentEditorChanged(editor);
        if (editor) {
            int line, index;
            editor->getCursorPosition(&line, &index);
            emit cursorMoved(line + 1, index + 1);
        }
    });
    connect(tabBar(), &QTabBar::customContextMenuRequested, this, &EditorWidget::onContextMenu);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &EditorWidget::onFileChangedOnDisk);
}

CodeEditor *EditorWidget::addEditor()
{
    auto *editor = new CodeEditor;
    editor->applySettings(*m_settings);

    connect(editor, &QsciScintilla::modificationChanged, this,
            [this, editor] { updateTabTitle(editor); });
    connect(editor, &QsciScintilla::textChanged, this, &EditorWidget::textEdited);
    connect(editor, &QsciScintilla::cursorPositionChanged, this,
            [this, editor](int line, int index) {
                if (editor == currentEditor())
                    emit cursorMoved(line + 1, index + 1);
            });
    connect(editor, &CodeEditor::breakpointToggled, this, [this, editor](int line, bool enabled) {
        if (!editor->isUntitled())
            emit breakpointToggled(editor->filePath(), line, enabled);
    });

    addTab(editor, QString());
    return editor;
}

CodeEditor *EditorWidget::newDocument(SourceLanguage language, bool withTemplate)
{
    CodeEditor *editor = addEditor();
    const bool cpp = language == SourceLanguage::Cpp;
    editor->setUntitledName(tr("Untitled %1.%2").arg(++m_untitledCounter)
                                .arg(cpp ? QStringLiteral("cpp") : QStringLiteral("c")));
    if (withTemplate) {
        editor->loadText(cpp ? FileManager::cppTemplate() : FileManager::cTemplate(),
                         EndOfLine::Unix);
    }
    updateTabTitle(editor);
    setCurrentWidget(editor);
    editor->setFocus();
    emit documentsChanged();
    return editor;
}

CodeEditor *EditorWidget::openFile(const QString &path)
{
    const QString absolute = canonical(path);
    if (CodeEditor *existing = editorForPath(absolute)) {
        setCurrentWidget(existing);
        existing->setFocus();
        return existing;
    }

    LoadedFile file;
    QString error;
    if (!FileManager::load(absolute, &file, &error)) {
        QMessageBox::warning(this, tr("Could Not Open File"),
                             tr("\"%1\" could not be opened.\n\n%2").arg(absolute, error));
        return nullptr;
    }

    CodeEditor *editor = addEditor();
    editor->setFilePath(absolute);
    editor->loadText(file.text, file.eol);
    editor->setSavedTimestamp(QFileInfo(absolute).lastModified());
    updateTabTitle(editor);
    m_watcher->addPath(absolute);
    m_settings->addRecentFile(absolute);
    setCurrentWidget(editor);
    editor->setFocus();

    if (file.lossyEncoding) {
        emit statusMessage(tr("%1 is not valid UTF-8 and was opened as Latin-1.")
                               .arg(editor->displayName()));
    }
    emit fileOpened(absolute);
    emit documentsChanged();
    return editor;
}

CodeEditor *EditorWidget::currentEditor() const
{
    return qobject_cast<CodeEditor *>(currentWidget());
}

CodeEditor *EditorWidget::editorForPath(const QString &path) const
{
    const QString target = canonical(path);
    for (CodeEditor *editor : editors()) {
        if (!editor->isUntitled() && editor->filePath() == target)
            return editor;
    }
    return nullptr;
}

QList<CodeEditor *> EditorWidget::editors() const
{
    QList<CodeEditor *> result;
    result.reserve(count());
    for (int i = 0; i < count(); ++i) {
        if (auto *editor = qobject_cast<CodeEditor *>(widget(i)))
            result.append(editor);
    }
    return result;
}

bool EditorWidget::writeTo(CodeEditor *editor, const QString &path)
{
    // Stops our own write from arriving as an external change.
    const bool watched = m_watcher->files().contains(path);
    if (watched)
        m_watcher->removePath(path);

    QString error;
    const bool saved = FileManager::save(path, editor->text(), &error);
    if (!saved) {
        QMessageBox::critical(this, tr("Could Not Save File"),
                              tr("\"%1\" could not be saved.\n\n%2").arg(path, error));
    } else {
        editor->setFilePath(canonical(path));
        editor->setModified(false);
        editor->setSavedTimestamp(QFileInfo(path).lastModified());
        updateTabTitle(editor);
    }

    if (saved || watched)
        m_watcher->addPath(saved ? editor->filePath() : path);
    return saved;
}

bool EditorWidget::save(CodeEditor *editor)
{
    if (!editor)
        return false;
    if (editor->isUntitled())
        return saveAs(editor);
    return writeTo(editor, editor->filePath());
}

bool EditorWidget::saveAs(CodeEditor *editor)
{
    if (!editor)
        return false;

    const QString suggestion = editor->isUntitled()
                                   ? editor->displayName().remove(QLatin1Char(' '))
                                   : editor->filePath();
    const QString path = QFileDialog::getSaveFileName(this, tr("Save File"), suggestion,
                                                      FileManager::openFileFilter());
    if (path.isEmpty())
        return false;

    const QString previous = editor->filePath();
    if (!writeTo(editor, path))
        return false;
    if (!previous.isEmpty() && previous != editor->filePath())
        m_watcher->removePath(previous);

    m_settings->addRecentFile(editor->filePath());
    emit documentsChanged();
    return true;
}

bool EditorWidget::saveAll()
{
    bool allSaved = true;
    for (CodeEditor *editor : editors()) {
        if (editor->isModified() && !save(editor))
            allSaved = false;
    }
    return allSaved;
}

bool EditorWidget::confirmClose(CodeEditor *editor)
{
    if (!editor->isModified())
        return true;

    setCurrentWidget(editor);
    QMessageBox box(this);
    box.setObjectName(QStringLiteral("unsavedChangesPrompt"));
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Unsaved Changes"));
    box.setText(tr("Save changes to \"%1\"?").arg(editor->displayName()));
    box.setInformativeText(tr("Your changes will be lost if you don't save them."));
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);

    switch (box.exec()) {
    case QMessageBox::Save: return save(editor);
    case QMessageBox::Discard: return true;
    default: return false;
    }
}

bool EditorWidget::closeEditor(CodeEditor *editor)
{
    if (!editor || !confirmClose(editor))
        return false;

    if (!editor->isUntitled())
        m_watcher->removePath(editor->filePath());
    removeTab(indexOf(editor));
    editor->deleteLater();
    emit documentsChanged();
    return true;
}

bool EditorWidget::closeAll()
{
    for (CodeEditor *editor : editors()) {
        if (!closeEditor(editor))
            return false;
    }
    return true;
}

bool EditorWidget::confirmCloseAll()
{
    for (CodeEditor *editor : editors()) {
        if (!confirmClose(editor))
            return false;
        // Discarded or saved; it must not be asked about a second time.
        editor->setModified(false);
    }
    return true;
}

void EditorWidget::updateTabTitle(CodeEditor *editor)
{
    const int index = indexOf(editor);
    if (index < 0)
        return;
    const QString name = editor->displayName();
    setTabText(index, editor->isModified() ? QStringLiteral("● ") + name : name);
    setTabToolTip(index, editor->isUntitled() ? name : editor->filePath());
    emit documentsChanged();
}

void EditorWidget::applySettings()
{
    for (CodeEditor *editor : editors())
        editor->applySettings(*m_settings);
}

void EditorWidget::applyDiagnostics(const QList<Diagnostic> &diagnostics)
{
    for (CodeEditor *editor : editors()) {
        if (editor->isUntitled())
            continue;
        QList<Diagnostic> own;
        for (const Diagnostic &diagnostic : diagnostics) {
            if (diagnostic.hasLocation() && canonical(diagnostic.file) == editor->filePath())
                own.append(diagnostic);
        }
        editor->setDiagnostics(own);
    }
}

void EditorWidget::clearDiagnostics()
{
    for (CodeEditor *editor : editors())
        editor->clearDiagnostics();
}

void EditorWidget::clearExecutionLines()
{
    for (CodeEditor *editor : editors())
        editor->setExecutionLine(0);
}

void EditorWidget::reload(CodeEditor *editor)
{
    LoadedFile file;
    QString error;
    if (!FileManager::load(editor->filePath(), &file, &error)) {
        emit statusMessage(tr("Could not reload %1: %2").arg(editor->displayName(), error));
        return;
    }

    int line, index;
    editor->getCursorPosition(&line, &index);
    const QList<int> breakpoints = editor->breakpointLines();
    editor->loadText(file.text, file.eol);
    for (const int breakpoint : breakpoints)
        editor->setBreakpoint(breakpoint, true);
    editor->setCursorPosition(line, index);
    editor->setSavedTimestamp(QFileInfo(editor->filePath()).lastModified());
    updateTabTitle(editor);
}

void EditorWidget::onFileChangedOnDisk(const QString &path)
{
    CodeEditor *editor = editorForPath(path);
    if (!editor)
        return;

    // Atomic saves replace the file, which drops the watch. Re-adding after a
    // short delay gives the replacement time to appear.
    QTimer::singleShot(rewatchDelayMs, this, [this, path] {
        if (editorForPath(path) && QFileInfo::exists(path) && !m_watcher->files().contains(path))
            m_watcher->addPath(path);
    });

    const QFileInfo info(path);
    if (!info.exists()) {
        editor->setModified(true);
        emit statusMessage(tr("%1 was deleted on disk.").arg(editor->displayName()));
        return;
    }
    if (info.lastModified() == editor->savedTimestamp())
        return;

    if (!editor->isModified()) {
        reload(editor);
        emit statusMessage(tr("Reloaded %1, which changed on disk.").arg(editor->displayName()));
        return;
    }

    setCurrentWidget(editor);
    const auto answer = QMessageBox::question(
        this, tr("File Changed on Disk"),
        tr("\"%1\" changed on disk, and you have unsaved changes.\n\n"
           "Reload it and lose your changes?").arg(editor->displayName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        reload(editor);
    else
        editor->setSavedTimestamp(info.lastModified());
}

void EditorWidget::onContextMenu(const QPoint &position)
{
    const int index = tabBar()->tabAt(position);
    auto *editor = qobject_cast<CodeEditor *>(widget(index));
    if (!editor)
        return;

    QMenu menu(this);
    menu.addAction(tr("Close"), this, [this, editor] { closeEditor(editor); });
    menu.addAction(tr("Close Others"), this, [this, editor] {
        for (CodeEditor *other : editors()) {
            if (other != editor && !closeEditor(other))
                return;
        }
    });
    menu.addAction(tr("Close All"), this, &EditorWidget::closeAll);
    menu.addSeparator();
    QAction *copyPath = menu.addAction(tr("Copy File Path"), this, [editor] {
        QApplication::clipboard()->setText(editor->filePath());
    });
    copyPath->setEnabled(!editor->isUntitled());
    menu.exec(tabBar()->mapToGlobal(position));
}
