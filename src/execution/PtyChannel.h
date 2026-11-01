#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

class QSocketNotifier;

// The master side of a pseudo-terminal, read and written through the event loop.
//
// Reads apply back-pressure: after each chunk the channel pauses for longer
// than the receiver took to handle it. A program printing in a tight loop then
// blocks in the kernel instead of starving the GUI thread.
class PtyChannel : public QObject
{
    Q_OBJECT

public:
    explicit PtyChannel(QObject *parent = nullptr);
    ~PtyChannel() override;

    // Allocates a new terminal for a process started elsewhere, such as GDB's
    // inferior. The slave stays open here so that the channel survives the
    // process exiting and restarting.
    bool open(QString *error);

    // Takes ownership of a master descriptor from forkpty().
    void adopt(int masterFd);

    QString slaveName() const;
    bool isOpen() const { return m_master >= 0; }

    void write(const QByteArray &data);
    void setWindowSize(int columns, int rows);

    // Reads everything currently buffered, ignoring back-pressure. Used once
    // the process has exited so that no final output is lost.
    void drain();

    void close();

signals:
    void dataReady(const QByteArray &data);

private:
    void attach(int masterFd);
    void onReadable();
    void flushWrites();

    int m_master = -1;
    int m_slave = -1;
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QTimer m_resumeTimer;
    QByteArray m_writeBuffer;
};
