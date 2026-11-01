#include "execution/ProcessRunner.h"

#include "execution/PtyChannel.h"

#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <linux/close_range.h>
#include <pty.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int killGraceMs = 1500;
constexpr int exitPollMs = 50;

std::vector<char *> pointersTo(QByteArrayList &strings)
{
    std::vector<char *> pointers;
    pointers.reserve(static_cast<size_t>(strings.size()) + 1);
    for (QByteArray &string : strings)
        pointers.push_back(string.data());
    pointers.push_back(nullptr);
    return pointers;
}

} // namespace

QString RunResult::description() const
{
    const QString seconds = QString::number(static_cast<double>(durationMs) / 1000.0, 'f', 3);
    if (stoppedByUser)
        return QObject::tr("Stopped by user after %1 s").arg(seconds);
    if (signal != 0) {
        return QObject::tr("Terminated by signal %1 (%2) after %3 s")
            .arg(signal)
            .arg(QString::fromLocal8Bit(::strsignal(signal)), seconds);
    }
    return QObject::tr("Process exited with code %1 after %2 s").arg(exitCode).arg(seconds);
}

ProcessRunner::ProcessRunner(QObject *parent)
    : QObject(parent)
    , m_pty(new PtyChannel(this))
{
    connect(m_pty, &PtyChannel::dataReady, this, &ProcessRunner::output);

    m_exitPoll.setInterval(exitPollMs);
    connect(&m_exitPoll, &QTimer::timeout, this, &ProcessRunner::checkExit);

    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_pid > 0)
            ::kill(-m_pid, SIGKILL);
    });
}

ProcessRunner::~ProcessRunner()
{
    if (m_pid > 0) {
        ::kill(-m_pid, SIGKILL);
        ::waitpid(m_pid, nullptr, 0);
    }
    if (m_pidFd >= 0)
        ::close(m_pidFd);
}

bool ProcessRunner::start(const RunRequest &request, QString *error)
{
    if (isRunning()) {
        *error = tr("A program is already running.");
        return false;
    }

    const QFileInfo program(request.program);
    if (!program.isFile() || !program.isExecutable()) {
        *error = tr("\"%1\" is not an executable file.").arg(request.program);
        return false;
    }
    const QString workingDirectory = request.workingDirectory.isEmpty()
                                         ? program.absolutePath()
                                         : request.workingDirectory;
    if (!QFileInfo(workingDirectory).isDir()) {
        *error = tr("Working directory \"%1\" does not exist.").arg(workingDirectory);
        return false;
    }

    // Everything the child needs is prepared here, because only
    // async-signal-safe calls are allowed between fork() and exec().
    const QByteArray path = QFile::encodeName(program.absoluteFilePath());
    const QByteArray directory = QFile::encodeName(workingDirectory);

    QByteArrayList argumentStrings{path};
    for (const QString &argument : request.arguments)
        argumentStrings << argument.toLocal8Bit();
    std::vector<char *> argv = pointersTo(argumentStrings);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    QByteArrayList environmentStrings;
    const QStringList entries = environment.toStringList();
    for (const QString &entry : entries)
        environmentStrings << entry.toLocal8Bit();
    std::vector<char *> envp = pointersTo(environmentStrings);

    // The child reports a failed exec through this pipe; it closes silently on success.
    int status[2];
    if (::pipe2(status, O_CLOEXEC) != 0) {
        *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }

    struct winsize size{};
    size.ws_col = static_cast<unsigned short>(m_columns);
    size.ws_row = static_cast<unsigned short>(m_rows);

    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, &size);
    if (pid < 0) {
        *error = QString::fromLocal8Bit(std::strerror(errno));
        ::close(status[0]);
        ::close(status[1]);
        return false;
    }

    if (pid == 0) {
        sigset_t none;
        ::sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        ::signal(SIGPIPE, SIG_DFL);
        ::syscall(SYS_close_range, 3U, ~0U, CLOSE_RANGE_CLOEXEC);

        if (::chdir(directory.constData()) == 0)
            ::execve(path.constData(), argv.data(), envp.data());

        const int failure = errno;
        [[maybe_unused]] const ssize_t ignored = ::write(status[1], &failure, sizeof failure);
        ::_exit(127);
    }

    ::close(status[1]);
    int failure = 0;
    ssize_t received;
    do {
        received = ::read(status[0], &failure, sizeof failure);
    } while (received < 0 && errno == EINTR);
    ::close(status[0]);

    if (received > 0) {
        ::waitpid(pid, nullptr, 0);
        ::close(master);
        *error = tr("Could not execute \"%1\": %2")
                     .arg(request.program, QString::fromLocal8Bit(std::strerror(failure)));
        return false;
    }

    m_pid = pid;
    m_stopping = false;
    m_clock.start();
    m_pty->adopt(master);
    watchExit();
    return true;
}

void ProcessRunner::watchExit()
{
    // A pidfd wakes the event loop exactly once, at exit. Kernels older than 5.3
    // lack it, so a poll remains as the fallback.
    m_pidFd = static_cast<int>(::syscall(SYS_pidfd_open, m_pid, 0));
    if (m_pidFd < 0) {
        m_exitPoll.start();
        return;
    }
    ::fcntl(m_pidFd, F_SETFD, FD_CLOEXEC);
    m_exitNotifier = new QSocketNotifier(m_pidFd, QSocketNotifier::Read, this);
    connect(m_exitNotifier, &QSocketNotifier::activated, this, &ProcessRunner::checkExit);
}

void ProcessRunner::checkExit()
{
    if (m_pid <= 0)
        return;

    int status = 0;
    const pid_t reaped = ::waitpid(m_pid, &status, WNOHANG);
    if (reaped == 0 || (reaped < 0 && errno == EINTR))
        return;

    m_exitPoll.stop();
    m_killTimer.stop();
    delete m_exitNotifier;
    m_exitNotifier = nullptr;
    if (m_pidFd >= 0) {
        ::close(m_pidFd);
        m_pidFd = -1;
    }

    // Anything the program started in its session goes with it.
    ::kill(-m_pid, SIGKILL);
    m_pid = -1;
    m_pty->drain();
    m_pty->close();

    RunResult result;
    result.durationMs = m_clock.elapsed();
    result.stoppedByUser = m_stopping;
    if (WIFEXITED(status))
        result.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        result.signal = WTERMSIG(status);
    emit finished(result);
}

void ProcessRunner::write(const QByteArray &data)
{
    m_pty->write(data);
}

void ProcessRunner::setWindowSize(int columns, int rows)
{
    m_columns = columns;
    m_rows = rows;
    m_pty->setWindowSize(columns, rows);
}

void ProcessRunner::stop()
{
    if (m_pid <= 0 || m_stopping)
        return;
    m_stopping = true;
    ::kill(-m_pid, SIGTERM);
    ::kill(-m_pid, SIGCONT);   // A process suspended with Ctrl+Z cannot act on SIGTERM.
    m_killTimer.start(killGraceMs);
}
