#pragma once

#include <QList>
#include <QString>

struct Symbol
{
    enum class Kind { Function, Class, Struct, Enum, Namespace };

    Kind kind = Kind::Function;
    QString name;
    int line = 0;   // 1-based.
};

// A regular-expression scan for definitions, for the Symbols outline and Go to
// Definition. It understands no C++ and will miss unusual declarations; that is
// the price of having no index to build and keep in memory.
namespace SymbolScanner {

QList<Symbol> scan(const QString &text);

// The line defining name, or 0 when text does not define it.
int findDefinition(const QString &text, const QString &name);

} // namespace SymbolScanner
