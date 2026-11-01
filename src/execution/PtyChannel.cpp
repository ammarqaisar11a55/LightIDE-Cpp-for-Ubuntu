#include "execution/PtyChannel.h"

#include <QElapsedTimer>
#include <QSocketNotifier>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace {

constexpr qsizetype chunkSize = 64 * 1024;
constexpr int minPauseMs = 2;
constexpr int maxPauseMs = 100;

} // namespace

PtyChannel::PtyChannel(QObject *parent)
    : QObject(parent)
{
    m_resumeTimer.setSingleShot(true);
    connect(&m_resumeTimer, &QTimer::timeout, this, [this] {
        if (m_readNotifier)
            m_readNotifier->setEnabled(true);
    });
}

PtyChannel::~PtyChannel()
{
    close();
}

bool PtyChannel::open(QString *error)
{
    close();

    const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        if (master >= 0)
            ::close(master);
        return false;
    }

    attach(master);
    m_slave = ::open(slaveName().toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    return true;
}

void PtyChannel::adopt(int masterFd)
{
    close();
    attach(masterFd);
}

void PtyChannel::attach(int masterFd)
{
    m_master = masterFd;
    ::fcntl(m_master, F_SETFL, ::fcntl(m_master, F_GETFL) | O_NONBLOCK);

    m_readNotifier = new QSocketNotifier(m_master, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &PtyChannel::onReadable);

    m_writeNotifier = new QSocketNotifier(m_master, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &PtyChannel::flushWrites);
}

QString PtyChannel::slaveName() const
{
    if (m_master < 0)
        return {};
    char name[128];
    return ::ptsname_r(m_master, name, sizeof name) == 0 ? QString::fromLocal8Bit(name) : QString();
}

void PtyChannel::write(const QByteArray &data)
{
    if (m_master < 0)
        return;
    m_writeBuffer.append(data);
    flushWrites();
}

void PtyChannel::flushWrites()
{
    while (m_master >= 0 && !m_writeBuffer.isEmpty()) {
        const ssize_t written = ::write(m_master, m_writeBuffer.constData(),
                                        static_cast<size_t>(m_writeBuffer.size()));
        if (written > 0) {
            m_writeBuffer.remove(0, written);
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        break;
    }
    // The terminal's input queue is full until the program reads from it.
    if (m_writeNotifier)
        m_writeNotifier->setEnabled(!m_writeBuffer.isEmpty());
}

void PtyChannel::setWindowSize(int columns, int rows)
{
    if (m_master < 0)
        return;
    struct winsize size{};
    size.ws_col = static_cast<unsigned short>(columns);
    size.ws_row = static_cast<unsigned short>(rows);
    ::ioctl(m_master, TIOCSWINSZ, &size);
}

void PtyChannel::onReadable()
{
    QByteArray chunk(chunkSize, Qt::Uninitialized);
    const ssize_t count = ::read(m_master, chunk.data(), static_cast<size_t>(chunk.size()));
    if (count < 0 && (errno == EAGAIN || errno == EINTR))
        return;
    if (count <= 0) {
        // EIO: every slave descriptor is closed. Exit is reported separately.
        m_readNotifier->setEnabled(false);
        return;
    }

    chunk.truncate(count);
    m_readNotifier->setEnabled(false);
    QElapsedTimer handling;
    handling.start();
    emit dataReady(chunk);

    if (m_readNotifier) {   // A receiver may have closed the channel.
        const int pause = static_cast<int>(handling.elapsed()) * 2;
        m_resumeTimer.start(std::clamp(pause, minPauseMs, maxPauseMs));
    }
}

void PtyChannel::drain()
{
    if (m_master < 0)
        return;

    m_resumeTimer.stop();
    QByteArray chunk(chunkSize, Qt::Uninitialized);
    for (;;) {
        const ssize_t count = ::read(m_master, chunk.data(), static_cast<size_t>(chunk.size()));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        emit dataReady(chunk.left(count));
    }
}

void PtyChannel::close()
{
    m_resumeTimer.stop();
    delete m_readNotifier;
    m_readNotifier = nullptr;
    delete m_writeNotifier;
    m_writeNotifier = nullptr;
    m_writeBuffer.clear();
    if (m_slave >= 0) {
        ::close(m_slave);
        m_slave = -1;
    }
    if (m_master >= 0) {
        ::close(m_master);
        m_master = -1;
    }
}
