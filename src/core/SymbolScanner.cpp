#include "core/SymbolScanner.h"

#include <QRegularExpression>
#include <QSet>

namespace {

// Replaces comments and string contents with spaces, keeping every newline so
// that offsets still map to the same line numbers.
QString blankCommentsAndStrings(const QString &text)
{
    QString out = text;
    enum class State { Code, LineComment, BlockComment, String, Character };
    State state = State::Code;

    for (qsizetype i = 0; i < out.size(); ++i) {
        const QChar c = out.at(i);
        const QChar next = i + 1 < out.size() ? out.at(i + 1) : QChar();
        switch (state) {
        case State::Code:
            if (c == u'/' && next == u'/')
                state = State::LineComment;
            else if (c == u'/' && next == u'*')
                state = State::BlockComment;
            else if (c == u'"')
                state = State::String;
            else if (c == u'\'')
                state = State::Character;
            break;
        case State::LineComment:
            if (c == u'\n')
                state = State::Code;
            else
                out[i] = u' ';
            break;
        case State::BlockComment:
            if (c == u'*' && next == u'/') {
                out[i] = u' ';
                out[i + 1] = u' ';
                ++i;
                state = State::Code;
            } else if (c != u'\n') {
                out[i] = u' ';
            }
            break;
        case State::String:
        case State::Character:
            if (c == u'\\') {
                out[i] = u' ';
                if (next != u'\n' && !next.isNull())
                    out[++i] = u' ';
            } else if (c == (state == State::String ? u'"' : u'\'') || c == u'\n') {
                state = State::Code;
            } else {
                out[i] = u' ';
            }
            break;
        }
    }
    return out;
}

int lineOfOffset(const QString &text, qsizetype offset, qsizetype &cursor, int &line)
{
    while (cursor < offset) {
        if (text.at(cursor) == u'\n')
            ++line;
        ++cursor;
    }
    return line;
}

bool isControlKeyword(const QString &word)
{
    static const QSet<QString> keywords = {
        "if",     "for",    "while",  "switch", "return", "sizeof",
        "catch",  "else",   "do",     "case",   "throw",  "new",
        "delete", "decltype", "alignof", "static_assert", "defined",
    };
    return keywords.contains(word);
}

} // namespace

namespace SymbolScanner {

QList<Symbol> scan(const QString &text)
{
    const QString code = blankCommentsAndStrings(text);

    // A definition, never a declaration: the name must lead to an opening brace.
    static const QRegularExpression pattern(QStringLiteral(
        R"((?:^|[;{}])\s*(?:template\s*<[^;{}]*>\s*)?)"
        R"((?:(class|struct|enum(?:\s+class)?|namespace)\s+(?:\[\[[^\]]*\]\]\s*)?(\w+)[^;(){}]*\{)"
        R"(|(?:[\w:<>,*&~\[\]]+[ \t\r\n*&]+)*?((?:\w+::)*~?\w+)\s*\([^;{}()]*(?:\([^()]*\)[^;{}()]*)*\)\s*(?:const|noexcept|override|final|\s)*\{))"),
        QRegularExpression::MultilineOption);

    QList<Symbol> symbols;
    qsizetype cursor = 0;
    int line = 1;

    auto it = pattern.globalMatch(code);
    while (it.hasNext()) {
        const auto match = it.next();
        Symbol symbol;
        qsizetype nameOffset;

        if (match.capturedLength(2) > 0) {
            const QString keyword = match.captured(1);
            symbol.name = match.captured(2);
            nameOffset = match.capturedStart(2);
            if (keyword.startsWith(QLatin1String("class")))
                symbol.kind = Symbol::Kind::Class;
            else if (keyword.startsWith(QLatin1String("struct")))
                symbol.kind = Symbol::Kind::Struct;
            else if (keyword.startsWith(QLatin1String("enum")))
                symbol.kind = Symbol::Kind::Enum;
            else
                symbol.kind = Symbol::Kind::Namespace;
        } else {
            symbol.name = match.captured(3);
            nameOffset = match.capturedStart(3);
            symbol.kind = Symbol::Kind::Function;
            const QString lastPart = symbol.name.section(QStringLiteral("::"), -1);
            if (symbol.name.isEmpty() || isControlKeyword(lastPart))
                continue;
        }

        symbol.line = lineOfOffset(code, nameOffset, cursor, line);
        symbols.append(symbol);
    }
    return symbols;
}

int findDefinition(const QString &text, const QString &name)
{
    const QList<Symbol> symbols = scan(text);
    for (const Symbol &symbol : symbols) {
        if (symbol.name == name || symbol.name.section(QStringLiteral("::"), -1) == name)
            return symbol.line;
    }
    return 0;
}

} // namespace SymbolScanner
