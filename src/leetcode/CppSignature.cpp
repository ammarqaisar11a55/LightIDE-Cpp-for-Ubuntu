#include "leetcode/CppSignature.h"

#include "leetcode/LcValue.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {

enum class TokenKind { Ident, Number, String, Punct };

struct Token
{
    TokenKind kind = TokenKind::Punct;
    QString text;
    qsizetype start = 0;
    qsizetype end = 0;
};

using Tokens = QList<Token>;

bool isIdentStart(QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'_';
}

bool isDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

bool isIdentChar(QChar c)
{
    return isIdentStart(c) || isDigit(c);
}

// Enough of C++ for LeetCode's starter code. A >> is always two > tokens, which
// is what declarations need.
Tokens tokenize(const QString &source)
{
    static const QStringList multiPunct = {
        QStringLiteral("::"), QStringLiteral("->"), QStringLiteral("..."), QStringLiteral("&&"),
        QStringLiteral("||"), QStringLiteral("=="), QStringLiteral("!="),  QStringLiteral("<="),
        QStringLiteral("+="), QStringLiteral("-="), QStringLiteral("++"),  QStringLiteral("--"),
    };

    Tokens tokens;
    qsizetype i = 0;
    const qsizetype n = source.size();
    const auto at = [&](qsizetype index) { return index < n ? source.at(index) : QChar(); };

    while (i < n) {
        const QChar c = source.at(i);
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (c == u'/' && at(i + 1) == u'/') {
            while (i < n && source.at(i) != u'\n')
                ++i;
            continue;
        }
        if (c == u'/' && at(i + 1) == u'*') {
            const qsizetype close = source.indexOf(QLatin1String("*/"), i + 2);
            i = close < 0 ? n : close + 2;
            continue;
        }
        if (c == u'#') {
            while (i < n && source.at(i) != u'\n') {
                if (source.at(i) == u'\\' && at(i + 1) == u'\n')
                    ++i;
                ++i;
            }
            continue;
        }

        const qsizetype start = i;
        if (isIdentStart(c)) {
            while (i < n && isIdentChar(source.at(i)))
                ++i;
            tokens.append({TokenKind::Ident, source.mid(start, i - start), start, i});
            continue;
        }
        if (isDigit(c)) {
            while (i < n && (isIdentChar(source.at(i)) || source.at(i) == u'.' || source.at(i) == u'\''))
                ++i;
            tokens.append({TokenKind::Number, source.mid(start, i - start), start, i});
            continue;
        }
        if (c == u'"' || c == u'\'') {
            ++i;
            while (i < n && source.at(i) != c)
                i += source.at(i) == u'\\' ? 2 : 1;
            ++i;
            i = std::min(i, n);
            tokens.append({TokenKind::String, source.mid(start, i - start), start, i});
            continue;
        }

        QString text(c);
        for (const QString &punct : multiPunct) {
            if (QStringView(source).sliced(i).startsWith(punct)) {
                text = punct;
                break;
            }
        }
        i += text.size();
        tokens.append({TokenKind::Punct, text, start, i});
    }
    return tokens;
}

// Index of the token closing the bracket at openIndex, or -1.
qsizetype findClosing(const Tokens &tokens, qsizetype openIndex)
{
    const QString open = tokens.at(openIndex).text;
    const QString close = open == QLatin1String("(")   ? QStringLiteral(")")
                          : open == QLatin1String("{") ? QStringLiteral("}")
                          : open == QLatin1String("[") ? QStringLiteral("]")
                                                       : QStringLiteral(">");
    int depth = 0;
    for (qsizetype i = openIndex; i < tokens.size(); ++i) {
        const QString &text = tokens.at(i).text;
        if (text == open) {
            ++depth;
        } else if (text == close && --depth == 0) {
            return i;
        }
    }
    return -1;
}

// Splits at commas that are not nested inside brackets.
QList<Tokens> splitTopLevel(const Tokens &tokens)
{
    QList<Tokens> parts;
    Tokens current;
    int depth = 0;
    for (const Token &token : tokens) {
        const QString &t = token.text;
        if (t == u"(" || t == u"<" || t == u"{" || t == u"[")
            ++depth;
        else if (t == u")" || t == u">" || t == u"}" || t == u"]")
            --depth;

        if (t == u"," && depth == 0) {
            parts.append(current);
            current.clear();
        } else {
            current.append(token);
        }
    }
    if (!current.isEmpty() || !parts.isEmpty())
        parts.append(current);
    return parts;
}

QString normalizeBuiltin(const QStringList &words)
{
    const bool isUnsigned = words.contains(QStringLiteral("unsigned"));
    const qsizetype longCount = words.count(QStringLiteral("long"));

    QStringList core;
    for (const QString &word : words) {
        if (word != u"unsigned" && word != u"signed" && word != u"long" && word != u"int")
            core << word;
    }

    QString name;
    if (!core.isEmpty()) {
        name = core.join(QLatin1Char(' '));
        if (name == u"double" && longCount > 0)
            name = QStringLiteral("long double");
    } else if (longCount >= 2) {
        name = QStringLiteral("long long");
    } else if (longCount == 1) {
        name = QStringLiteral("long");
    } else {
        name = QStringLiteral("int");
    }
    if (words.contains(QStringLiteral("short")))
        name = QStringLiteral("short");
    return isUnsigned ? QStringLiteral("unsigned ") + name : name;
}

CppType parseTypeTokens(const Tokens &tokens, qsizetype &i)
{
    static const QSet<QString> builtinWords = {
        QStringLiteral("unsigned"), QStringLiteral("signed"), QStringLiteral("long"),
        QStringLiteral("short"),    QStringLiteral("int"),    QStringLiteral("char"),
        QStringLiteral("double"),   QStringLiteral("float"),  QStringLiteral("bool"),
        QStringLiteral("void"),
    };

    CppType type;
    const auto peek = [&](qsizetype offset = 0) -> QString {
        return i + offset < tokens.size() ? tokens.at(i + offset).text : QString();
    };
    const auto kindAt = [&](qsizetype index) {
        return index < tokens.size() ? tokens.at(index).kind : TokenKind::Punct;
    };

    while (peek() == u"const") {
        type.isConst = true;
        ++i;
    }

    if (builtinWords.contains(peek())) {
        QStringList words;
        while (builtinWords.contains(peek()))
            words << tokens.at(i++).text;
        type.name = normalizeBuiltin(words);
    } else {
        QStringList parts;
        if (peek() == u"::")
            ++i;
        while (i < tokens.size() && tokens.at(i).kind == TokenKind::Ident) {
            parts << tokens.at(i++).text;
            if (peek() == u"::" && kindAt(i + 1) == TokenKind::Ident && i + 1 < tokens.size()) {
                ++i;
                continue;
            }
            break;
        }
        if (parts.isEmpty()) {
            throw LcError(QStringLiteral("Expected a type name but found \"%1\"")
                              .arg(i < tokens.size() ? peek() : QStringLiteral("end of input")));
        }
        if (parts.first() == u"std" && parts.size() > 1)
            parts.removeFirst();
        type.name = parts.join(QStringLiteral("::"));
    }

    if (peek() == u"<") {
        ++i;
        while (peek() != u">") {
            if (i >= tokens.size())
                throw LcError(QStringLiteral("Unterminated template argument list"));
            type.templateArgs.append(parseTypeTokens(tokens, i));
            if (peek() == u",")
                ++i;
        }
        ++i;
    }

    for (;;) {
        const QString t = peek();
        if (t == u"const") {
            type.isConst = type.pointerDepth == 0 ? true : type.isConst;
            ++i;
        } else if (t == u"*") {
            ++type.pointerDepth;
            ++i;
        } else if (t == u"&" || t == u"&&") {
            type.isReference = true;
            ++i;
        } else {
            break;
        }
    }
    return type;
}

bool isTypeKeyword(const QString &word)
{
    static const QSet<QString> keywords = {
        QStringLiteral("int"),   QStringLiteral("long"),     QStringLiteral("short"),
        QStringLiteral("char"),  QStringLiteral("double"),   QStringLiteral("float"),
        QStringLiteral("bool"),  QStringLiteral("unsigned"), QStringLiteral("signed"),
        QStringLiteral("const"),
    };
    return keywords.contains(word);
}

QList<CppParam> parseParams(const Tokens &tokens, const QString &code)
{
    if (tokens.isEmpty() || (tokens.size() == 1 && tokens.first().text == u"void"))
        return {};

    QList<CppParam> params;
    const QList<Tokens> parts = splitTopLevel(tokens);
    for (qsizetype index = 0; index < parts.size(); ++index) {
        Tokens decl = parts.at(index);
        const auto equals = std::find_if(decl.cbegin(), decl.cend(),
                                         [](const Token &t) { return t.text == u"="; });
        if (equals != decl.cend())
            decl = decl.first(equals - decl.cbegin());
        if (decl.isEmpty())
            throw LcError(QStringLiteral("Empty parameter in method signature"));

        const Token &last = decl.last();
        const bool hasName = decl.size() > 1 && last.kind == TokenKind::Ident && !isTypeKeyword(last.text);
        const Tokens typeTokens = hasName ? decl.first(decl.size() - 1) : decl;
        const qsizetype typeStart = typeTokens.first().start;
        const QString typeText = code.mid(typeStart, typeTokens.last().end - typeStart);

        CppParam param;
        param.name = hasName ? last.text : QStringLiteral("arg%1").arg(index + 1);
        param.typeText = typeText;
        param.type = CppSignature::parseType(typeText);
        params.append(std::move(param));
    }
    return params;
}

std::optional<CppMethod> buildMethod(const Tokens &head, const Tokens &paramTokens,
                                     const QString &className, const QString &code)
{
    static const QSet<QString> declSpecifiers = {
        QStringLiteral("static"), QStringLiteral("virtual"),   QStringLiteral("inline"),
        QStringLiteral("explicit"), QStringLiteral("constexpr"), QStringLiteral("friend"),
    };

    bool isStatic = false;
    Tokens rest;
    for (const Token &token : head) {
        if (declSpecifiers.contains(token.text))
            isStatic = isStatic || token.text == u"static";
        else
            rest.append(token);
    }
    if (rest.isEmpty())
        return std::nullopt;

    const Token nameToken = rest.last();
    const bool isOperator = std::any_of(rest.cbegin(), rest.cend(), [](const Token &t) {
        return t.text == u"operator" || t.text == u"~";
    });
    if (nameToken.kind != TokenKind::Ident || isOperator)
        return std::nullopt;

    CppMethod method;
    method.name = nameToken.text;
    method.isConstructor = nameToken.text == className && rest.size() == 1;
    method.isStatic = isStatic;
    const Tokens returnTokens = rest.first(rest.size() - 1);
    if (!returnTokens.isEmpty()) {
        const qsizetype start = returnTokens.first().start;
        method.returnTypeText = code.mid(start, returnTokens.last().end - start);
        method.returnType = CppSignature::parseType(method.returnTypeText);
        method.hasReturnType = true;
    }
    method.params = parseParams(paramTokens, code);
    return method;
}

QList<CppMethod> parseClassBody(const Tokens &body, const QString &className, bool isStruct,
                                const QString &code)
{
    QList<CppMethod> methods;
    bool isPublic = isStruct;
    qsizetype i = 0;

    while (i < body.size()) {
        const QString &first = body.at(i).text;
        if ((first == u"public" || first == u"private" || first == u"protected")
            && i + 1 < body.size() && body.at(i + 1).text == u":") {
            isPublic = first == u"public";
            i += 2;
            continue;
        }

        // Collect one member declaration up to ( ; or { at depth 0.
        const qsizetype declStart = i;
        int angle = 0;
        while (i < body.size()) {
            const QString &x = body.at(i).text;
            if (x == u"<")
                ++angle;
            else if (x == u">")
                --angle;
            else if (angle == 0 && (x == u"(" || x == u";" || x == u"{"))
                break;
            ++i;
        }
        if (i >= body.size())
            break;

        const QString stop = body.at(i).text;
        if (stop == u";") {
            ++i;
            continue;
        }
        if (stop == u"{") {
            // A nested type or brace-initialized field.
            const qsizetype close = findClosing(body, i);
            i = close < 0 ? body.size() : close + 1;
            continue;
        }

        const Tokens head = body.mid(declStart, i - declStart);
        const qsizetype parenClose = findClosing(body, i);
        if (parenClose < 0)
            throw LcError(QStringLiteral("Unbalanced parentheses in class %1").arg(className));
        const Tokens paramTokens = body.mid(i + 1, parenClose - i - 1);
        i = parenClose + 1;

        // Skip qualifiers and any initializer list.
        while (i < body.size() && body.at(i).text != u"{" && body.at(i).text != u";") {
            if (body.at(i).text == u"(")
                i = findClosing(body, i);
            ++i;
        }
        if (i < body.size() && body.at(i).text == u"{") {
            const qsizetype close = findClosing(body, i);
            i = close < 0 ? body.size() : close + 1;
        } else {
            ++i;
        }

        const auto method = buildMethod(head, paramTokens, className, code);
        if (method && isPublic)
            methods.append(*method);
    }
    return methods;
}

QList<CppClass> parseClasses(const QString &code)
{
    const Tokens tokens = tokenize(code);
    QList<CppClass> classes;
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const Token &t = tokens.at(i);
        if ((t.text != u"class" && t.text != u"struct") || i + 1 >= tokens.size()
            || tokens.at(i + 1).kind != TokenKind::Ident) {
            continue;
        }

        // A ; before the opening brace means a forward declaration.
        qsizetype j = i + 2;
        while (j < tokens.size() && tokens.at(j).text != u"{" && tokens.at(j).text != u";")
            ++j;
        if (j >= tokens.size() || tokens.at(j).text != u"{")
            continue;

        const qsizetype close = findClosing(tokens, j);
        const QString name = tokens.at(i + 1).text;
        if (close < 0)
            throw LcError(QStringLiteral("Unbalanced braces in class %1").arg(name));

        classes.append({name, parseClassBody(tokens.mid(j + 1, close - j - 1), name, t.text == u"struct", code)});
        i = close;
    }
    return classes;
}

QString stripBlockDecoration(const QString &line)
{
    static const QRegularExpression decoration(QStringLiteral(R"(^\s*\* ?)"));
    QString out = line;
    return out.remove(decoration);
}

// The text of each block comment and each run of // lines, undecorated.
QStringList extractComments(const QString &code)
{
    static const QRegularExpression lineEnd(QStringLiteral("\\r?\\n"));
    static const QRegularExpression leadingStars(QStringLiteral(R"(^\*+)"));
    static const QRegularExpression lineCommentStart(QStringLiteral("^// ?"));

    QStringList comments;
    QStringList lineGroup;
    QStringList block;
    bool inBlock = false;

    const auto flushLines = [&] {
        if (!lineGroup.isEmpty()) {
            comments << lineGroup.join(QLatin1Char('\n'));
            lineGroup.clear();
        }
    };

    const QStringList lines = code.split(lineEnd);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (inBlock) {
            const qsizetype end = line.indexOf(QLatin1String("*/"));
            if (end >= 0) {
                block << stripBlockDecoration(line.left(end));
                comments << block.join(QLatin1Char('\n'));
                block.clear();
                inBlock = false;
            } else {
                block << stripBlockDecoration(line);
            }
            continue;
        }
        if (trimmed.startsWith(QLatin1String("/*"))) {
            flushLines();
            QString after = line.mid(line.indexOf(QLatin1String("/*")) + 2);
            after.remove(leadingStars);
            const qsizetype end = after.indexOf(QLatin1String("*/"));
            if (end >= 0) {
                comments << after.left(end);
            } else {
                inBlock = true;
                block = {stripBlockDecoration(after)};
            }
            continue;
        }
        if (trimmed.startsWith(QLatin1String("//"))) {
            QString content = trimmed;
            lineGroup << content.remove(lineCommentStart);
            continue;
        }
        flushLines();
    }
    flushLines();
    return comments;
}

qsizetype leadingSpaces(const QString &line)
{
    qsizetype count = 0;
    while (count < line.size() && line.at(count) == u' ')
        ++count;
    return count;
}

QString dedent(const QString &text)
{
    const QStringList lines = text.split(u'\n');
    qsizetype minimum = -1;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (!lines.at(i).trimmed().isEmpty()) {
            const qsizetype indent = leadingSpaces(lines.at(i));
            minimum = minimum < 0 ? indent : std::min(minimum, indent);
        }
    }
    minimum = std::max<qsizetype>(minimum, 0);

    // Nested lines are indented relative to the closing brace; keep that relationship.
    const qsizetype closingIndent = lines.size() > 1 ? leadingSpaces(lines.last()) : 0;
    const qsizetype cut = std::min(minimum, closingIndent);

    QStringList out;
    for (qsizetype i = 0; i < lines.size(); ++i)
        out << (i == 0 ? lines.at(i).trimmed() : lines.at(i).mid(cut));
    return out.join(QLatin1Char('\n'));
}

QList<CommentedDefinition> extractCommentedDefinitions(const QString &code)
{
    QList<CommentedDefinition> definitions;
    const QStringList comments = extractComments(code);
    for (const QString &comment : comments) {
        const Tokens tokens = tokenize(comment);
        for (qsizetype i = 0; i < tokens.size(); ++i) {
            const Token &t = tokens.at(i);
            if ((t.text != u"struct" && t.text != u"class") || i + 1 >= tokens.size()
                || tokens.at(i + 1).kind != TokenKind::Ident) {
                continue;
            }
            const QString name = tokens.at(i + 1).text;
            qsizetype j = i + 2;
            while (j < tokens.size() && tokens.at(j).text != u"{" && tokens.at(j).text != u";")
                ++j;
            if (j >= tokens.size() || tokens.at(j).text != u"{")
                continue;

            const qsizetype close = findClosing(tokens, j);
            if (close < 0)
                break;

            qsizetype end = tokens.at(close).end;
            if (close + 1 < tokens.size() && tokens.at(close + 1).text == u";")
                end = tokens.at(close + 1).end;

            const bool known = std::any_of(definitions.cbegin(), definitions.cend(),
                                           [&](const CommentedDefinition &d) { return d.name == name; });
            if (name != u"Solution" && !known)
                definitions.append({name, dedent(comment.mid(t.start, end - t.start))});
            i = close;
        }
    }
    return definitions;
}

} // namespace

bool CppType::isInteger() const
{
    static const QSet<QString> names = {
        QStringLiteral("int"),           QStringLiteral("long"),
        QStringLiteral("long long"),     QStringLiteral("short"),
        QStringLiteral("unsigned int"),  QStringLiteral("unsigned long"),
        QStringLiteral("unsigned long long"), QStringLiteral("unsigned short"),
        QStringLiteral("size_t"),        QStringLiteral("int8_t"),
        QStringLiteral("int16_t"),       QStringLiteral("int32_t"),
        QStringLiteral("int64_t"),       QStringLiteral("uint8_t"),
        QStringLiteral("uint16_t"),      QStringLiteral("uint32_t"),
        QStringLiteral("uint64_t"),
    };
    return pointerDepth == 0 && names.contains(name);
}

bool CppType::isFloating() const
{
    return pointerDepth == 0 && (name == u"double" || name == u"float" || name == u"long double");
}

bool CppType::isString() const
{
    return pointerDepth == 0 && name == u"string";
}

bool CppType::isVector() const
{
    return pointerDepth == 0 && name == u"vector" && !templateArgs.isEmpty();
}

bool CppType::isNodePointer(const QString &nodeName) const
{
    return pointerDepth == 1 && name == nodeName;
}

namespace CppSignature {

CppStarter analyzeStarterCode(const QString &code)
{
    return {code, parseClasses(code), extractCommentedDefinitions(code)};
}

CppType parseType(const QString &text)
{
    static const QSet<QString> ignoredWords = {
        QStringLiteral("struct"), QStringLiteral("class"), QStringLiteral("typename"),
        QStringLiteral("volatile"), QStringLiteral("mutable"),
    };

    Tokens tokens;
    const Tokens all = tokenize(text);
    for (const Token &token : all) {
        if (!ignoredWords.contains(token.text))
            tokens.append(token);
    }

    qsizetype i = 0;
    CppType type = parseTypeTokens(tokens, i);
    if (i != tokens.size()) {
        throw LcError(QStringLiteral("Unexpected \"%1\" in C++ type \"%2\"")
                          .arg(tokens.at(i).text, text));
    }
    return type;
}

QString formatType(const CppType &type, bool asValue)
{
    QString text = type.name;
    if (!type.templateArgs.isEmpty()) {
        QStringList args;
        for (const CppType &arg : type.templateArgs)
            args << formatType(arg);
        text += QLatin1Char('<') + args.join(QStringLiteral(", ")) + QLatin1Char('>');
    }
    if (type.isConst && !asValue)
        text = QStringLiteral("const ") + text;
    text += QString(type.pointerDepth, QLatin1Char('*'));
    if (type.isReference && !asValue)
        text += QLatin1Char('&');
    return text;
}

} // namespace CppSignature
