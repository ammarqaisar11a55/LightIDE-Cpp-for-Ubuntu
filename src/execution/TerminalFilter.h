#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringDecoder>

// Reduces a raw terminal byte stream to the few operations a line-oriented view
// can show. Holds state between calls, so an escape sequence, a UTF-8 character
// or a CR LF pair may be split across two reads.
class TerminalFilter
{
public:
    struct Op
    {
        enum class Kind { Text, Newline, CarriageReturn, Backspace };
        Kind kind;
        QString text;   // Only for Text.
    };

    QList<Op> feed(const QByteArray &data);
    void reset();

private:
    enum class State { Ground, Escape, Csi, Osc, OscEscape };

    void flushText(QList<Op> &ops);

    State m_state = State::Ground;
    bool m_pendingCarriageReturn = false;
    QByteArray m_textBytes;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
};
