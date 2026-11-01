#pragma once

#include "core/Settings.h"

#include <Qsci/qscilexercpp.h>

#include <QColor>
#include <QFont>
#include <QList>
#include <QString>

// QScintilla's keyword lists stop at C++03; this adds C++11 through C++23 and
// the standard library's common type names.
class CppLexer : public QsciLexerCPP
{
    Q_OBJECT

public:
    explicit CppLexer(QObject *parent = nullptr);
    const char *keywords(int set) const override;
};

namespace SyntaxHighlighter {

struct Token
{
    QString key;     // Stored in settings.
    QString label;   // Shown in the settings dialog.
};

QList<Token> tokens();
QColor defaultColor(ThemeKind theme, const QString &key);

// The colour in effect: the user's override, or the theme default.
QColor color(const AppearanceSettings &appearance, const QString &key);

void apply(QsciLexerCPP *lexer, const AppearanceSettings &appearance, const QFont &font);

} // namespace SyntaxHighlighter
