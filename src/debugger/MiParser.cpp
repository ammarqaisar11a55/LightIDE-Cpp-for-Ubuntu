#include "debugger/MiParser.h"

namespace {

class Cursor
{
public:
    explicit Cursor(const QByteArray &data)
        : m_data(data)
    {
    }

    bool atEnd() const { return m_position >= m_data.size(); }
    char peek() const { return atEnd() ? '\0' : m_data.at(m_position); }
    void advance() { ++m_position; }

    bool accept(char expected)
    {
        if (peek() != expected)
            return false;
        advance();
        return true;
    }

    int number()
    {
        int value = -1;
        while (peek() >= '0' && peek() <= '9') {
            value = (value < 0 ? 0 : value * 10) + (peek() - '0');
            advance();
        }
        return value;
    }

    QString word()
    {
        const qsizetype start = m_position;
        while (!atEnd() && peek() != '=' && peek() != ',' && peek() != '{' && peek() != '['
               && peek() != ']' && peek() != '}' && peek() != '"') {
            advance();
        }
        return QString::fromUtf8(m_data.mid(start, m_position - start));
    }

    // Parses a C string. GDB writes non-ASCII as octal escapes of UTF-8 bytes,
    // so bytes are collected first and decoded once at the end.
    QString cString()
    {
        QByteArray bytes;
        if (!accept('"'))
            return {};
        while (!atEnd() && peek() != '"') {
            char c = peek();
            advance();
            if (c != '\\') {
                bytes.append(c);
                continue;
            }
            c = peek();
            advance();
            switch (c) {
            case 'n': bytes.append('\n'); break;
            case 't': bytes.append('\t'); break;
            case 'r': bytes.append('\r'); break;
            case 'f': bytes.append('\f'); break;
            case 'b': bytes.append('\b'); break;
            case 'v': bytes.append('\v'); break;
            case 'a': bytes.append('\a'); break;
            case 'e': bytes.append('\x1b'); break;
            default:
                if (c >= '0' && c <= '7') {
                    int value = c - '0';
                    for (int i = 0; i < 2 && peek() >= '0' && peek() <= '7'; ++i) {
                        value = value * 8 + (peek() - '0');
                        advance();
                    }
                    bytes.append(static_cast<char>(value));
                } else {
                    bytes.append(c);
                }
                break;
            }
        }
        accept('"');
        return QString::fromUtf8(bytes);
    }

    QByteArray rest() const { return m_data.mid(m_position); }

private:
    const QByteArray &m_data;
    qsizetype m_position = 0;
};

MiValue parseValue(Cursor &cursor);

void parseResults(Cursor &cursor, MiValue &into, char terminator)
{
    while (!cursor.atEnd() && cursor.peek() != terminator) {
        const char next = cursor.peek();
        if (next == '"' || next == '{' || next == '[') {
            into.entries.append({QString(), parseValue(cursor)});
        } else {
            QString name = cursor.word();
            if (!cursor.accept('='))
                return;
            into.entries.append({std::move(name), parseValue(cursor)});
        }
        if (!cursor.accept(','))
            break;
    }
}

MiValue parseValue(Cursor &cursor)
{
    MiValue value;
    switch (cursor.peek()) {
    case '"':
        value.string = cursor.cString();
        break;
    case '{':
        cursor.advance();
        value.kind = MiValue::Kind::Tuple;
        parseResults(cursor, value, '}');
        cursor.accept('}');
        break;
    case '[':
        cursor.advance();
        value.kind = MiValue::Kind::List;
        parseResults(cursor, value, ']');
        cursor.accept(']');
        break;
    default:
        break;
    }
    return value;
}

} // namespace

const MiValue *MiValue::find(const QString &name) const
{
    for (const auto &entry : entries) {
        if (entry.first == name)
            return &entry.second;
    }
    return nullptr;
}

QString MiValue::text(const QString &name) const
{
    const MiValue *value = find(name);
    return value ? value->string : QString();
}

QList<MiValue> MiValue::items() const
{
    QList<MiValue> values;
    values.reserve(entries.size());
    for (const auto &entry : entries)
        values.append(entry.second);
    return values;
}

MiRecord MiParser::parse(const QByteArray &line)
{
    MiRecord record;
    record.raw = QString::fromUtf8(line);

    QByteArray trimmed = line;
    while (trimmed.endsWith('\n') || trimmed.endsWith('\r'))
        trimmed.chop(1);

    if (trimmed.trimmed() == "(gdb)") {
        record.type = MiRecord::Type::Prompt;
        return record;
    }

    Cursor cursor(trimmed);
    record.token = cursor.number();

    const char prefix = cursor.peek();
    cursor.advance();
    switch (prefix) {
    case '^': record.type = MiRecord::Type::Result; break;
    case '*': record.type = MiRecord::Type::ExecAsync; break;
    case '+': record.type = MiRecord::Type::StatusAsync; break;
    case '=': record.type = MiRecord::Type::NotifyAsync; break;
    case '~': record.type = MiRecord::Type::ConsoleStream; break;
    case '@': record.type = MiRecord::Type::TargetStream; break;
    case '&': record.type = MiRecord::Type::LogStream; break;
    default:
        return record;
    }

    if (prefix == '~' || prefix == '@' || prefix == '&') {
        record.stream = cursor.cString();
        return record;
    }

    record.resultClass = cursor.word();
    record.payload.kind = MiValue::Kind::Tuple;
    if (cursor.accept(','))
        parseResults(cursor, record.payload, '\0');
    return record;
}

QString MiParser::quote(const QString &text)
{
    QString quoted;
    quoted.reserve(text.size() + 2);
    quoted += QLatin1Char('"');
    for (const QChar c : text) {
        if (c == QLatin1Char('"') || c == QLatin1Char('\\'))
            quoted += QLatin1Char('\\');
        if (c == QLatin1Char('\n'))
            quoted += QLatin1String("\\n");
        else
            quoted += c;
    }
    quoted += QLatin1Char('"');
    return quoted;
}
