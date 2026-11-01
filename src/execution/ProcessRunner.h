#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <sys/types.h>

class PtyChannel;
class QSocketNotifier;

struct RunRequest
{
    QString program;   // Absolute path to the executable.
    QStringList arguments;
    QString workingDirectory;
};

struct RunResult
{
    int exitCode = 0;
    int signal = 0;              // Non-zero when a signal ended the process.
    bool stoppedByUser = false;
    qint64 durationMs = 0;

    bool crashed() const { return signal != 0 && !stoppedByUser; }
    QString description() const;
};

// Runs the user's program in its own session on a pseudo-terminal. See
// docs/ARCHITECTURE.md for why this is not a QProcess.
class ProcessRunner : public QObject
{
    Q_OBJECT

public:
    explicit ProcessRunner(QObject *parent = nullptr);
    ~ProcessRunner() override;

    bool start(const RunRequest &request, QString *error);
    bool isRunning() const { return m_pid > 0; }
    qint64 pid() const { return m_pid; }

    void write(const QByteArray &data);
    void setWindowSize(int columns, int rows);

    // Sends SIGTERM to the process group, then SIGKILL if it does not exit.
    void stop();

signals:
    void output(const QByteArray &data);
    void finished(const RunResult &result);

private:
    void watchExit();
    void checkExit();

    PtyChannel *m_pty;
    pid_t m_pid = -1;
    int m_pidFd = -1;
    QSocketNotifier *m_exitNotifier = nullptr;
    QTimer m_exitPoll;
    QTimer m_killTimer;
    QElapsedTimer m_clock;
    bool m_stopping = false;
    int m_columns = 80;
    int m_rows = 24;
};
