#pragma once

#include "debugger/MiParser.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

class PtyChannel;
class QProcess;

struct SourceLocation
{
    QString file;
    int line = 0;

    bool operator==(const SourceLocation &other) const
    {
        return line == other.line && file == other.file;
    }
};

struct StackFrame
{
    int level = 0;
    QString function;
    QString file;   // Empty when the frame has no debug information.
    int line = 0;
    QString address;
};

struct Variable
{
    QString name;
    QString type;
    QString value;
    bool isArgument = false;
};

struct DebugRequest
{
    QString gdbPath;
    QString program;
    QString arguments;
    QString workingDirectory;
    QList<SourceLocation> breakpoints;
};

// Drives GDB over the MI protocol. The program being debugged gets its own
// pseudo-terminal, whose output arrives through inferiorOutput().
class DebuggerManager : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Starting, Running, Paused, Stopping };
    Q_ENUM(State)

    explicit DebuggerManager(QObject *parent = nullptr);
    ~DebuggerManager() override;

    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Idle; }

    void start(const DebugRequest &request);
    void stop();

    void continueExecution();
    void stepInto();
    void stepOver();
    void stepOut();
    void interrupt();

    void addBreakpoint(const SourceLocation &location);
    void removeBreakpoint(const SourceLocation &location);

    void selectFrame(int level);
    void evaluate(const QString &expression);
    void runConsoleCommand(const QString &command);
    void writeToProgram(const QByteArray &data);

signals:
    void stateChanged(DebuggerManager::State state);
    void paused(const QString &file, int line, const QString &reason);
    void resumed();
    void framesChanged(const QList<StackFrame> &frames);
    void variablesChanged(const QList<Variable> &variables);
    void frameSelected(const QString &file, int line);
    void consoleOutput(const QString &text);
    void inferiorOutput(const QByteArray &data);
    void programExited(int exitCode);
    void errorOccurred(const QString &message);
    void sessionEnded();

private:
    using Callback = std::function<void(const MiRecord &)>;

    void setState(State state);
    void resume(const QString &command);
    void send(const QString &command, Callback callback = {});
    void onStarted();
    void onReadable();
    void handleRecord(const MiRecord &record);
    void handleStopped(const MiRecord &record);
    void insertBreakpoint(const SourceLocation &location);
    void flushBreakpointChanges();
    void refreshStack();
    void refreshVariables();
    void onFinished();

    QProcess *m_gdb = nullptr;
    PtyChannel *m_inferiorPty;
    QTimer m_killTimer;
    State m_state = State::Idle;
    DebugRequest m_request;
    QByteArray m_buffer;
    int m_nextToken = 1;
    QHash<int, Callback> m_callbacks;
    QList<QPair<SourceLocation, int>> m_breakpoints;   // Location and GDB's number.
    QList<SourceLocation> m_pendingAdds;
    QList<SourceLocation> m_pendingRemovals;
    QList<StackFrame> m_frames;
    bool m_programExited = false;
};
