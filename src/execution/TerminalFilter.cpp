#include "execution/TerminalFilter.h"

namespace {

constexpr char escape = '\x1b';
constexpr char bell = '\x07';

} // namespace

void TerminalFilter::reset()
{
    m_state = State::Ground;
    m_pendingCarriageReturn = false;
    m_textBytes.clear();
    m_decoder = QStringDecoder(QStringDecoder::Utf8);
}

void TerminalFilter::flushText(QList<Op> &ops)
{
    if (m_textBytes.isEmpty())
        return;
    QString text = m_decoder.decode(m_textBytes);
    m_textBytes.clear();
    if (!text.isEmpty())
        ops.append(Op{Op::Kind::Text, std::move(text)});
}

QList<TerminalFilter::Op> TerminalFilter::feed(const QByteArray &data)
{
    QList<Op> ops;

    for (const char byte : data) {
        switch (m_state) {
        case State::Escape:
            if (byte == '[')
                m_state = State::Csi;
            else if (byte == ']')
                m_state = State::Osc;
            else
                m_state = State::Ground;
            continue;
        case State::Csi:
            // Parameters are 0x20-0x3F; the first byte from 0x40 up ends the sequence.
            if (byte >= 0x40 && byte <= 0x7e)
                m_state = State::Ground;
            continue;
        case State::Osc:
            if (byte == bell)
                m_state = State::Ground;
            else if (byte == escape)
                m_state = State::OscEscape;
            continue;
        case State::OscEscape:
            m_state = byte == '\\' ? State::Ground : State::Osc;
            continue;
        case State::Ground:
            break;
        }

        if (m_pendingCarriageReturn) {
            m_pendingCarriageReturn = false;
            if (byte == '\n') {
                flushText(ops);
                ops.append(Op{Op::Kind::Newline, {}});
                continue;
            }
            flushText(ops);
            ops.append(Op{Op::Kind::CarriageReturn, {}});
        }

        switch (byte) {
        case escape:
            m_state = State::Escape;
            break;
        case '\r':
            m_pendingCarriageReturn = true;
            break;
        case '\n':
            flushText(ops);
            ops.append(Op{Op::Kind::Newline, {}});
            break;
        case '\b':
            flushText(ops);
            ops.append(Op{Op::Kind::Backspace, {}});
            break;
        case '\t':
            m_textBytes.append(byte);
            break;
        default:
            // Other C0 controls have no visible meaning here. Bytes above 0x7F
            // are negative when char is signed and belong to UTF-8 sequences.
            if (static_cast<unsigned char>(byte) >= 0x20 && byte != 0x7f)
                m_textBytes.append(byte);
            break;
        }
    }

    flushText(ops);
    return ops;
}
