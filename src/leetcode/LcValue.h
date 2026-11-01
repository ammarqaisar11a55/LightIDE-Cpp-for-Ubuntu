#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include <optional>
#include <stdexcept>

// Raised by the LeetCode code for problems worth showing to the user as they are.
class LcError : public std::runtime_error
{
public:
    explicit LcError(const QString &message)
        : std::runtime_error(message.toStdString())
    {
    }

    QString message() const { return QString::fromStdString(what()); }
};

// A value in LeetCode's test-case notation, which is a superset of JSON.
//
// Numbers keep their source text, so 64-bit integers never lose precision and
// doubles keep LeetCode's formatting ("2.00000"). Symbols cover the bare tokens
// LeetCode uses in outputs: # as a level separator, _ for a don't-care slot.
struct LcValue
{
    enum class Kind { Null, Bool, Number, String, Array, Object, Symbol };

    Kind kind = Kind::Null;
    bool boolean = false;
    QString text;   // Number text, string contents or symbol.
    QList<LcValue> items;
    QList<QPair<QString, LcValue>> entries;

    static LcValue makeNumber(const QString &text);
    static LcValue makeString(const QString &value);
    static LcValue makeArray(const QList<LcValue> &items);

    bool isNumber() const { return kind == Kind::Number; }
    bool isArray() const { return kind == Kind::Array; }
    bool isObject() const { return kind == Kind::Object; }
    bool isNull() const { return kind == Kind::Null; }

    const LcValue *entry(const QString &key) const;
};

bool lcIsIntegerText(const QString &text);

namespace LcValueParser {

// Accepts strict JSON plus bare symbols, single-quoted strings, trailing commas
// and unquoted object keys. Throws LcError on anything else.
LcValue parse(const QString &text);
std::optional<LcValue> tryParse(const QString &text);

// Parses one value from the start of text and leaves the remainder in rest.
LcValue parsePrefix(const QString &text, QString *rest);

} // namespace LcValueParser

// Compact LeetCode notation: [1,2,3], "abc", null. An object with a "return"
// entry renders the way LeetCode shows in-place outputs: 2, nums = [2,2,_,_].
QString toLeetCodeText(const LcValue &value);

// Pretty JSON for tests.json, keeping short arrays on one line.
QString toJsonText(const LcValue &value, int indent = 0);

// The text as a JSON string literal.
QString jsonString(const QString &text);
