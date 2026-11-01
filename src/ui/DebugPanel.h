#pragma once

#include "debugger/DebuggerManager.h"

#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;

// The Debug Console tab: variables and call stack beside a GDB console.
class DebugPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DebugPanel(QWidget *parent = nullptr);

    void setVariables(const QList<Variable> &variables);
    void setFrames(const QList<StackFrame> &frames);
    void appendConsole(const QString &text);
    void clearState();
    void clearAll();

    QTreeWidget *variablesView() const { return m_variables; }
    QTreeWidget *framesView() const { return m_frames; }
    QString consoleText() const;

signals:
    void frameActivated(int level);
    void commandEntered(const QString &command);

private:
    QTreeWidget *m_variables;
    QTreeWidget *m_frames;
    QPlainTextEdit *m_console;
    QLineEdit *m_input;
};
