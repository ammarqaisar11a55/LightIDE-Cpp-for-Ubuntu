#pragma once

#include "compiler/Diagnostic.h"
#include "core/Settings.h"

#include <QList>
#include <QTabWidget>

class DebugPanel;
class LeetCodePanel;
class QPlainTextEdit;
class QTreeWidget;
class TerminalView;

// The bottom panel: Compiler Output, Build Messages, Application Output and
// Debug Console.
class OutputPanel : public QTabWidget
{
    Q_OBJECT

public:
    enum Tab { CompilerTab, MessagesTab, ApplicationTab, DebugTab, LeetCodeTab };

    explicit OutputPanel(QWidget *parent = nullptr);

    TerminalView *terminal() const { return m_terminal; }
    DebugPanel *debugPanel() const { return m_debug; }
    LeetCodePanel *leetCodePanel() const { return m_leetCode; }
    QPlainTextEdit *compilerOutput() const { return m_compiler; }
    QTreeWidget *messages() const { return m_messages; }

    void setTheme(ThemeKind theme);

    void clearBuild();
    void appendCommand(const QString &command);
    void appendOutputLine(const QString &line);
    void appendBuildStatus(const QString &text, bool success);
    void addDiagnostic(const Diagnostic &diagnostic);

    QList<Diagnostic> diagnostics() const { return m_diagnostics; }

    // Activates the diagnostic on the compiler output line, as a click would.
    bool activateCompilerLine(int blockNumber);

signals:
    void diagnosticActivated(const Diagnostic &diagnostic);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void appendColored(const QString &text, const QColor &color);
    void updateMessagesTabTitle();

    QPlainTextEdit *m_compiler;
    QTreeWidget *m_messages;
    TerminalView *m_terminal;
    DebugPanel *m_debug;
    LeetCodePanel *m_leetCode;
    QList<Diagnostic> m_diagnostics;
    ThemeKind m_theme = ThemeKind::Dark;
    int m_errors = 0;
    int m_warnings = 0;
};
