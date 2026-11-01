#pragma once

#include "compiler/CompileCommand.h"
#include "compiler/Diagnostic.h"
#include "core/Settings.h"

#include <QList>
#include <QTabWidget>

class CodeEditor;
class QFileSystemWatcher;

// The tabbed set of open documents, and everything about their lifecycle:
// opening, saving, unsaved-change prompts and changes made on disk.
class EditorWidget : public QTabWidget
{
    Q_OBJECT

public:
    explicit EditorWidget(Settings *settings, QWidget *parent = nullptr);

    CodeEditor *newDocument(SourceLanguage language, bool withTemplate = true);
    CodeEditor *openFile(const QString &path);

    CodeEditor *currentEditor() const;
    CodeEditor *editorForPath(const QString &path) const;
    QList<CodeEditor *> editors() const;

    bool save(CodeEditor *editor);
    bool saveAs(CodeEditor *editor);
    bool saveAll();

    bool closeEditor(CodeEditor *editor);
    bool closeAll();

    // Asks about each unsaved file; false means the user cancelled.
    bool confirmCloseAll();

    // Where Save As starts for a file that has never been saved.
    void setDefaultDirectory(const QString &directory) { m_defaultDirectory = directory; }
    QString suggestedSavePath(const CodeEditor *editor) const;

    void applySettings();
    void applyDiagnostics(const QList<Diagnostic> &diagnostics);
    void clearDiagnostics();
    void clearExecutionLines();

signals:
    void currentEditorChanged(CodeEditor *editor);
    void documentsChanged();
    void cursorMoved(int line, int column);
    void textEdited();
    void breakpointToggled(const QString &path, int line, bool enabled);
    void fileOpened(const QString &path);
    void statusMessage(const QString &message);

private:
    CodeEditor *addEditor();
    bool writeTo(CodeEditor *editor, const QString &path);
    bool confirmClose(CodeEditor *editor);
    void updateTabTitle(CodeEditor *editor);
    void onFileChangedOnDisk(const QString &path);
    void reload(CodeEditor *editor);
    void onContextMenu(const QPoint &position);

    Settings *m_settings;
    QFileSystemWatcher *m_watcher;
    QString m_defaultDirectory;
    int m_untitledCounter = 0;
};
