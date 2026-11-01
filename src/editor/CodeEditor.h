#pragma once

#include "compiler/Diagnostic.h"
#include "core/Settings.h"
#include "filesystem/FileManager.h"

#include <Qsci/qsciscintilla.h>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

class CppLexer;

class CodeEditor : public QsciScintilla
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    // Shown on the tab; for an unsaved file this is a name such as "Untitled 1.cpp".
    QString displayName() const;
    void setUntitledName(const QString &name) { m_untitledName = name; }

    bool isUntitled() const { return m_filePath.isEmpty(); }

    QDateTime savedTimestamp() const { return m_savedTimestamp; }
    void setSavedTimestamp(const QDateTime &timestamp) { m_savedTimestamp = timestamp; }

    void applySettings(const Settings &settings);

    // Replaces the document without leaving the load on the undo stack.
    void loadText(const QString &text, EndOfLine eol);
    EndOfLine lineEnding() const;
    void setLineEnding(EndOfLine eol);

    // Line numbers below are 1-based, as in compiler output and GDB.
    void toggleBreakpoint(int line);
    void setBreakpoint(int line, bool enabled);
    QList<int> breakpointLines() const;
    void setExecutionLine(int line);   // 0 clears it.

    void setDiagnostics(const QList<Diagnostic> &diagnostics);
    void clearDiagnostics();

    void goToPosition(int line, int column = 0);
    void toggleLineComment();
    QString wordAtCursor() const;

signals:
    void breakpointToggled(int line, bool enabled);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;

private:
    enum Marker {
        BreakpointMarker = 0,
        ExecutionArrowMarker = 1,
        ExecutionLineMarker = 2,
        ErrorMarker = 3,
        WarningMarker = 4,
    };
    enum Indicator { ErrorIndicator = 8, WarningIndicator = 9 };
    enum Column { LineNumberColumn = 0, SymbolColumn = 1, FoldColumn = 2 };

    void onMarginClicked(int margin, int line, Qt::KeyboardModifiers modifiers);
    void onCharAdded(int character);
    void autoCloseBracket(int character);
    void updateLineNumberMargin();
    void styleFoldMarkers(const QColor &color);
    void showDiagnosticTip(int position, int x, int y);
    int charAt(long position) const;

    CppLexer *m_lexer;
    QString m_filePath;
    QString m_untitledName;
    QDateTime m_savedTimestamp;
    QHash<int, QString> m_lineMessages;   // 0-based line to diagnostic text.
    int m_executionLine = 0;
    int m_lineNumberDigits = 0;
    bool m_showLineNumbers = true;
    bool m_autoCloseBrackets = true;
};
