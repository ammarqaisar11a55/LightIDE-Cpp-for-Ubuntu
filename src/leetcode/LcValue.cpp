#include "leetcode/LcValue.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

constexpr int inlineLimit = 100;

bool isDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

bool isWordStart(QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'_' || c == u'#' || c == u'$';
}

bool isWordChar(QChar c)
{
    return isWordStart(c) || isDigit(c) || c == u'.';
}

bool isSymbolChar(QChar c)
{
    return !c.isSpace() && c != u'[' && c != u']' && c != u'{' && c != u'}' && c != u',' && c != u':'
           && c != u'"' && c != u'\'';
}

QString abbreviate(const QString &text)
{
    return text.size() > 80 ? text.left(77) + QStringLiteral("...") : text;
}

class Parser
{
public:
    explicit Parser(const QString &text)
        : m_text(text)
    {
    }

    QString rest() const { return m_text.mid(m_pos); }
    bool atEnd() const { return m_pos >= m_text.size(); }

    void skipWhitespace()
    {
        while (!atEnd() && m_text.at(m_pos).isSpace())
            ++m_pos;
    }

    LcValue parseValue()
    {
        skipWhitespace();
        if (atEnd())
            fail(QStringLiteral("Unexpected end of input"));

        const QChar c = m_text.at(m_pos);
        if (c == u'[')
            return parseArray();
        if (c == u'{')
            return parseObject();
        if (c == u'"' || c == u'\'')
            return LcValue::makeString(parseString());

        const qsizetype numberLength = scanNumber();
        if (numberLength > 0) {
            QString number = m_text.mid(m_pos, numberLength);
            m_pos += numberLength;
            if (number.startsWith(u'+'))
                number.remove(0, 1);
            return LcValue::makeNumber(number);
        }

        if (isWordStart(c)) {
            const qsizetype start = m_pos;
            while (!atEnd() && isWordChar(m_text.at(m_pos)))
                ++m_pos;
            return fromWord(m_text.mid(start, m_pos - start));
        }

        if (isSymbolChar(c)) {
            const qsizetype start = m_pos;
            while (!atEnd() && isSymbolChar(m_text.at(m_pos)))
                ++m_pos;
            LcValue value;
            value.kind = LcValue::Kind::Symbol;
            value.text = m_text.mid(start, m_pos - start);
            return value;
        }

        fail(QStringLiteral("Unexpected character \"%1\"").arg(c));
    }

    [[noreturn]] void fail(const QString &message) const
    {
        throw LcError(QStringLiteral("%1 at position %2 in: %3")
                          .arg(message)
                          .arg(m_pos)
                          .arg(abbreviate(m_text)));
    }

private:
    // [-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?
    qsizetype scanNumber() const
    {
        qsizetype i = m_pos;
        const qsizetype size = m_text.size();
        if (i < size && (m_text.at(i) == u'-' || m_text.at(i) == u'+'))
            ++i;

        const qsizetype digitsStart = i;
        while (i < size && isDigit(m_text.at(i)))
            ++i;
        if (i > digitsStart) {
            if (i < size && m_text.at(i) == u'.') {
                ++i;
                while (i < size && isDigit(m_text.at(i)))
                    ++i;
            }
        } else {
            if (i >= size || m_text.at(i) != u'.')
                return 0;
            ++i;
            const qsizetype fractionStart = i;
            while (i < size && isDigit(m_text.at(i)))
                ++i;
            if (i == fractionStart)
                return 0;
        }

        if (i < size && (m_text.at(i) == u'e' || m_text.at(i) == u'E')) {
            qsizetype e = i + 1;
            if (e < size && (m_text.at(e) == u'-' || m_text.at(e) == u'+'))
                ++e;
            const qsizetype exponentStart = e;
            while (e < size && isDigit(m_text.at(e)))
                ++e;
            if (e > exponentStart)
                i = e;
        }
        return i - m_pos;
    }

    static LcValue fromWord(const QString &word)
    {
        LcValue value;
        if (word == QLatin1String("null") || word == QLatin1String("None")
            || word == QLatin1String("nullptr")) {
            return value;
        }
        if (word == QLatin1String("true") || word == QLatin1String("True")) {
            value.kind = LcValue::Kind::Bool;
            value.boolean = true;
            return value;
        }
        if (word == QLatin1String("false") || word == QLatin1String("False")) {
            value.kind = LcValue::Kind::Bool;
            return value;
        }
        value.kind = LcValue::Kind::Symbol;
        value.text = word;
        return value;
    }

    LcValue parseArray()
    {
        expect(u'[');
        LcValue value;
        value.kind = LcValue::Kind::Array;
        skipWhitespace();
        while (!consume(u']')) {
            value.items.append(parseValue());
            skipWhitespace();
            if (consume(u']'))
                break;
            expect(u',');
            skipWhitespace();
        }
        return value;
    }

    LcValue parseObject()
    {
        expect(u'{');
        LcValue value;
        value.kind = LcValue::Kind::Object;
        skipWhitespace();
        while (!consume(u'}')) {
            QString key;
            const QChar c = atEnd() ? QChar() : m_text.at(m_pos);
            if (c == u'"' || c == u'\'') {
                key = parseString();
            } else {
                if (!isWordStart(c))
                    fail(QStringLiteral("Expected object key"));
                const qsizetype start = m_pos;
                while (!atEnd() && isWordChar(m_text.at(m_pos)))
                    ++m_pos;
                key = m_text.mid(start, m_pos - start);
            }
            skipWhitespace();
            expect(u':');
            LcValue member = parseValue();
            value.entries.append({key, std::move(member)});
            skipWhitespace();
            if (consume(u'}'))
                break;
            expect(u',');
            skipWhitespace();
        }
        return value;
    }

    QString parseString()
    {
        const QChar quote = m_text.at(m_pos++);
        QString out;
        for (;;) {
            if (atEnd())
                fail(QStringLiteral("Unterminated string"));
            const QChar c = m_text.at(m_pos++);
            if (c == quote)
                return out;
            if (c != u'\\') {
                out += c;
                continue;
            }
            if (atEnd())
                fail(QStringLiteral("Unterminated escape"));
            const QChar escape = m_text.at(m_pos++);
            switch (escape.unicode()) {
            case 'n': out += u'\n'; break;
            case 't': out += u'\t'; break;
            case 'r': out += u'\r'; break;
            case 'b': out += u'\b'; break;
            case 'f': out += u'\f'; break;
            case '0': out += QChar(); break;
            case 'u': {
                bool ok = false;
                const ushort code = m_text.mid(m_pos, 4).toUShort(&ok, 16);
                if (!ok || m_pos + 4 > m_text.size())
                    fail(QStringLiteral("Invalid \\u escape"));
                out += QChar(code);
                m_pos += 4;
                break;
            }
            default:
                out += escape;   // \" \\ \/ \'
                break;
            }
        }
    }

    bool consume(QChar c)
    {
        if (!atEnd() && m_text.at(m_pos) == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    void expect(QChar c)
    {
        if (consume(c))
            return;
        const QString found = atEnd() ? QStringLiteral("end of input")
                                      : QStringLiteral("\"%1\"").arg(m_text.at(m_pos));
        fail(QStringLiteral("Expected \"%1\" but found %2").arg(c).arg(found));
    }

    const QString &m_text;
    qsizetype m_pos = 0;
};

// JSON forbids forms like .5, 5. or +5. LeetCode never uses them, but custom tests might.
QString normalizeJsonNumber(const QString &text)
{
    QString t = text.startsWith(u'+') ? text.mid(1) : text;
    static const QRegularExpression leadingPoint(QStringLiteral(R"(^(-?)\.)"));
    static const QRegularExpression trailingPoint(QStringLiteral(R"(\.(?=$|[eE]))"));
    t.replace(leadingPoint, QStringLiteral("\\10."));
    t.replace(trailingPoint, QStringLiteral(".0"));
    return t;
}

QString toInlineJson(const LcValue &value)
{
    switch (value.kind) {
    case LcValue::Kind::Null: return QStringLiteral("null");
    case LcValue::Kind::Bool: return value.boolean ? QStringLiteral("true") : QStringLiteral("false");
    case LcValue::Kind::Number: return normalizeJsonNumber(value.text);
    case LcValue::Kind::String:
    case LcValue::Kind::Symbol: return jsonString(value.text);
    case LcValue::Kind::Array: {
        QStringList parts;
        for (const LcValue &item : value.items)
            parts << toInlineJson(item);
        return QLatin1Char('[') + parts.join(QStringLiteral(", ")) + QLatin1Char(']');
    }
    case LcValue::Kind::Object: {
        QStringList parts;
        for (const auto &entry : value.entries)
            parts << jsonString(entry.first) + QStringLiteral(": ") + toInlineJson(entry.second);
        return QStringLiteral("{ ") + parts.join(QStringLiteral(", ")) + QStringLiteral(" }");
    }
    }
    return {};
}

} // namespace

LcValue LcValue::makeNumber(const QString &text)
{
    LcValue value;
    value.kind = Kind::Number;
    value.text = text;
    return value;
}

LcValue LcValue::makeString(const QString &text)
{
    LcValue value;
    value.kind = Kind::String;
    value.text = text;
    return value;
}

LcValue LcValue::makeArray(const QList<LcValue> &items)
{
    LcValue value;
    value.kind = Kind::Array;
    value.items = items;
    return value;
}

const LcValue *LcValue::entry(const QString &key) const
{
    for (const auto &member : entries) {
        if (member.first == key)
            return &member.second;
    }
    return nullptr;
}

bool lcIsIntegerText(const QString &text)
{
    static const QRegularExpression integer(QStringLiteral(R"(^[-+]?\d+$)"));
    return integer.match(text).hasMatch();
}

namespace LcValueParser {

LcValue parse(const QString &text)
{
    Parser parser(text);
    LcValue value = parser.parseValue();
    parser.skipWhitespace();
    if (!parser.atEnd())
        parser.fail(QStringLiteral("Unexpected trailing text \"%1\"").arg(parser.rest().left(20)));
    return value;
}

std::optional<LcValue> tryParse(const QString &text)
{
    try {
        return parse(text);
    } catch (const LcError &) {
        return std::nullopt;
    }
}

LcValue parsePrefix(const QString &text, QString *rest)
{
    Parser parser(text);
    LcValue value = parser.parseValue();
    *rest = parser.rest();
    return value;
}

} // namespace LcValueParser

QString toLeetCodeText(const LcValue &value)
{
    switch (value.kind) {
    case LcValue::Kind::Null: return QStringLiteral("null");
    case LcValue::Kind::Bool: return value.boolean ? QStringLiteral("true") : QStringLiteral("false");
    case LcValue::Kind::Number:
    case LcValue::Kind::Symbol: return value.text;
    case LcValue::Kind::String: return jsonString(value.text);
    case LcValue::Kind::Array: {
        QStringList parts;
        for (const LcValue &item : value.items)
            parts << toLeetCodeText(item);
        return QLatin1Char('[') + parts.join(QLatin1Char(',')) + QLatin1Char(']');
    }
    case LcValue::Kind::Object: {
        QStringList parts;
        if (const LcValue *result = value.entry(QStringLiteral("return"))) {
            parts << toLeetCodeText(*result);
            for (const auto &entry : value.entries) {
                if (entry.first != QLatin1String("return"))
                    parts << entry.first + QStringLiteral(" = ") + toLeetCodeText(entry.second);
            }
            return parts.join(QStringLiteral(", "));
        }
        for (const auto &entry : value.entries)
            parts << jsonString(entry.first) + QLatin1Char(':') + toLeetCodeText(entry.second);
        return QLatin1Char('{') + parts.join(QLatin1Char(',')) + QLatin1Char('}');
    }
    }
    return {};
}

QString toJsonText(const LcValue &value, int indent)
{
    const QString compact = toInlineJson(value);
    const bool container = value.isArray() || value.isObject();
    if (compact.size() + indent <= inlineLimit || !container)
        return compact;

    const QString pad(indent + 4, QLatin1Char(' '));
    const QString close(indent, QLatin1Char(' '));
    QStringList parts;
    if (value.isArray()) {
        for (const LcValue &item : value.items)
            parts << pad + toJsonText(item, indent + 4);
        return QStringLiteral("[\n") + parts.join(QStringLiteral(",\n")) + QLatin1Char('\n') + close
               + QLatin1Char(']');
    }
    for (const auto &entry : value.entries) {
        parts << pad + jsonString(entry.first) + QStringLiteral(": ")
                     + toJsonText(entry.second, indent + 4);
    }
    return QStringLiteral("{\n") + parts.join(QStringLiteral(",\n")) + QLatin1Char('\n') + close
           + QLatin1Char('}');
}

QString jsonString(const QString &text)
{
    QString out;
    out.reserve(text.size() + 2);
    out += u'"';
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '"': out += QStringLiteral("\\\""); break;
        case '\\': out += QStringLiteral("\\\\"); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        case '\t': out += QStringLiteral("\\t"); break;
        case '\b': out += QStringLiteral("\\b"); break;
        case '\f': out += QStringLiteral("\\f"); break;
        default:
            if (c.unicode() < 0x20)
                out += QStringLiteral("\\u%1").arg(int(c.unicode()), 4, 16, QLatin1Char('0'));
            else
                out += c;
            break;
        }
    }
    out += u'"';
    return out;
}
