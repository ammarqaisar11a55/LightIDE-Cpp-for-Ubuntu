#include "editor/SyntaxHighlighter.h"

#include "ui/Theme.h"

#include <QHash>

namespace {

const QHash<QString, QColor> darkSyntax = {
    {QStringLiteral("default"), QColor("#d8dde6")},
    {QStringLiteral("comment"), QColor("#667285")},
    {QStringLiteral("keyword"), QColor("#c89bf0")},
    {QStringLiteral("type"), QColor("#5fc6d8")},
    {QStringLiteral("string"), QColor("#9ccf8e")},
    {QStringLiteral("number"), QColor("#f0a868")},
    {QStringLiteral("preprocessor"), QColor("#e88fb5")},
    {QStringLiteral("operator"), QColor("#a5afc0")},
};

const QHash<QString, QColor> lightSyntax = {
    {QStringLiteral("default"), QColor("#1f2430")},
    {QStringLiteral("comment"), QColor("#6e7781")},
    {QStringLiteral("keyword"), QColor("#7c3aed")},
    {QStringLiteral("type"), QColor("#0b7a8f")},
    {QStringLiteral("string"), QColor("#1a7f37")},
    {QStringLiteral("number"), QColor("#b35900")},
    {QStringLiteral("preprocessor"), QColor("#a8327f")},
    {QStringLiteral("operator"), QColor("#4a5261")},
};

} // namespace

CppLexer::CppLexer(QObject *parent)
    : QsciLexerCPP(parent)
{
    setFoldComments(true);
    setFoldCompact(false);
    setStylePreprocessor(true);
}

const char *CppLexer::keywords(int set) const
{
    if (set == 1) {
        return "alignas alignof and and_eq asm auto bitand bitor bool break case catch char "
               "char8_t char16_t char32_t class co_await co_return co_yield compl concept const "
               "consteval constexpr constinit const_cast continue decltype default delete do "
               "double dynamic_cast else enum explicit export extern false final float for friend "
               "goto if import inline int long module mutable namespace new noexcept not not_eq "
               "nullptr operator or or_eq override private protected public register "
               "reinterpret_cast requires restrict return short signed sizeof static static_assert "
               "static_cast struct switch template this thread_local throw true try typedef typeid "
               "typename union unsigned using virtual void volatile wchar_t while xor xor_eq "
               "_Bool _Complex _Generic _Noreturn _Static_assert _Thread_local";
    }
    if (set == 2) {
        return "array bitset cin cerr clog complex cout deque endl FILE forward_list function "
               "int8_t int16_t int32_t int64_t intptr_t list map multimap multiset nullptr_t "
               "optional pair priority_queue ptrdiff_t queue set shared_ptr size_t ssize_t stack "
               "std string string_view stringstream tuple uint8_t uint16_t uint32_t uint64_t "
               "uintptr_t unique_ptr unordered_map unordered_multimap unordered_multiset "
               "unordered_set variant vector weak_ptr wstring";
    }
    return QsciLexerCPP::keywords(set);
}

namespace SyntaxHighlighter {

QList<Token> tokens()
{
    return {
        {QStringLiteral("default"), QObject::tr("Default text")},
        {QStringLiteral("comment"), QObject::tr("Comments")},
        {QStringLiteral("keyword"), QObject::tr("Keywords")},
        {QStringLiteral("type"), QObject::tr("Standard types")},
        {QStringLiteral("string"), QObject::tr("Strings")},
        {QStringLiteral("number"), QObject::tr("Numbers")},
        {QStringLiteral("preprocessor"), QObject::tr("Preprocessor")},
        {QStringLiteral("operator"), QObject::tr("Operators")},
    };
}

QColor defaultColor(ThemeKind theme, const QString &key)
{
    return (theme == ThemeKind::Dark ? darkSyntax : lightSyntax).value(key);
}

QColor color(const AppearanceSettings &appearance, const QString &key)
{
    const QString overridden = appearance.syntaxColors.value(key);
    return overridden.isEmpty() ? defaultColor(appearance.theme, key) : QColor(overridden);
}

void apply(QsciLexerCPP *lexer, const AppearanceSettings &appearance, const QFont &font)
{
    const ThemeColors &theme = Theme::colors(appearance.theme);
    const QColor background = appearance.editorBackground.isEmpty()
                                  ? theme.editorBackground
                                  : QColor(appearance.editorBackground);

    lexer->setDefaultPaper(background);
    lexer->setDefaultFont(font);
    lexer->setDefaultColor(color(appearance, QStringLiteral("default")));
    lexer->setPaper(background, -1);
    lexer->setFont(font, -1);
    lexer->setColor(color(appearance, QStringLiteral("default")), -1);

    const auto paint = [&](const QString &key, std::initializer_list<int> styles) {
        const QColor tokenColor = color(appearance, key);
        for (const int style : styles) {
            lexer->setColor(tokenColor, style);
            // Styles in inactive #if branches sit 64 above their active twin.
            lexer->setColor(tokenColor, style + 64);
        }
    };

    paint(QStringLiteral("comment"),
          {QsciLexerCPP::Comment, QsciLexerCPP::CommentLine, QsciLexerCPP::CommentDoc,
           QsciLexerCPP::CommentLineDoc, QsciLexerCPP::CommentDocKeyword,
           QsciLexerCPP::CommentDocKeywordError, QsciLexerCPP::PreProcessorComment,
           QsciLexerCPP::PreProcessorCommentLineDoc});
    paint(QStringLiteral("keyword"), {QsciLexerCPP::Keyword});
    paint(QStringLiteral("type"), {QsciLexerCPP::KeywordSet2, QsciLexerCPP::GlobalClass});
    paint(QStringLiteral("string"),
          {QsciLexerCPP::DoubleQuotedString, QsciLexerCPP::SingleQuotedString,
           QsciLexerCPP::RawString, QsciLexerCPP::VerbatimString, QsciLexerCPP::UnclosedString,
           QsciLexerCPP::HashQuotedString, QsciLexerCPP::TripleQuotedVerbatimString,
           QsciLexerCPP::EscapeSequence});
    paint(QStringLiteral("number"), {QsciLexerCPP::Number, QsciLexerCPP::UserLiteral});
    paint(QStringLiteral("preprocessor"), {QsciLexerCPP::PreProcessor});
    paint(QStringLiteral("operator"), {QsciLexerCPP::Operator});

    // QScintilla paints unterminated strings with a bright full-width band.
    lexer->setPaper(background, QsciLexerCPP::UnclosedString);
    lexer->setEolFill(false, QsciLexerCPP::UnclosedString);
}

} // namespace SyntaxHighlighter
