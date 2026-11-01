#include "leetcode/OutputComparator.h"

#include "leetcode/HarnessGenerator.h"
#include "leetcode/QuestionParser.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <csignal>

namespace {

constexpr double floatTolerance = 1e-5;
constexpr int keyDigits = 31;

bool valuesEqual(const LcValue &actual, const LcValue &expected, ComparisonMode mode, bool topLevel);

// Sign, then digits without leading zeros, so "+05" and "5" compare equal.
QString canonicalInteger(const QString &text)
{
    const bool negative = text.startsWith(u'-');
    QString digits = text;
    digits.remove(QRegularExpression(QStringLiteral(R"(^[-+]?0*)")));
    if (digits.isEmpty())
        return QStringLiteral("0");
    return negative ? QLatin1Char('-') + digits : digits;
}

bool numbersEqual(const QString &a, const QString &b)
{
    if (lcIsIntegerText(a) && lcIsIntegerText(b))
        return canonicalInteger(a) == canonicalInteger(b);

    bool okX = false;
    bool okY = false;
    const double x = a.toDouble(&okX);
    const double y = b.toDouble(&okY);
    if (!okX || !okY)
        return a == b;
    const double difference = std::abs(x - y);
    return difference <= floatTolerance || difference <= floatTolerance * std::abs(y);
}

QString canonicalKey(const LcValue &value, bool deep)
{
    switch (value.kind) {
    case LcValue::Kind::Null: return QStringLiteral("n");
    case LcValue::Kind::Bool: return value.boolean ? QStringLiteral("b1") : QStringLiteral("b0");
    case LcValue::Kind::Number: {
        if (!lcIsIntegerText(value.text))
            return QStringLiteral("f%1").arg(value.text.toDouble(), 0, 'f', 4);
        const QString canonical = canonicalInteger(value.text);
        if (!canonical.startsWith(u'-'))
            return QStringLiteral("i+") + canonical.rightJustified(keyDigits, QLatin1Char('0'));
        // Nines' complement, so that string order matches numeric order for negatives.
        QString digits = canonical.mid(1).rightJustified(keyDigits, QLatin1Char('0'));
        for (QChar &digit : digits)
            digit = QChar(u'0' + (u'9' - digit.unicode()));
        return QStringLiteral("i-") + digits;
    }
    case LcValue::Kind::String:
    case LcValue::Kind::Symbol: return QLatin1Char('s') + value.text;
    case LcValue::Kind::Array: {
        QStringList keys;
        for (const LcValue &item : value.items)
            keys << canonicalKey(item, deep);
        if (deep)
            keys.sort();
        return QLatin1Char('[') + keys.join(QString()) + QLatin1Char(']');
    }
    case LcValue::Kind::Object: {
        QString key = QStringLiteral("{");
        for (const auto &entry : value.entries)
            key += entry.first + QLatin1Char(':') + canonicalKey(entry.second, deep);
        return key + QLatin1Char('}');
    }
    }
    return {};
}

bool arraysEqual(const QList<LcValue> &actual, const QList<LcValue> &expected, ComparisonMode mode)
{
    if (actual.size() != expected.size())
        return false;
    for (qsizetype i = 0; i < actual.size(); ++i) {
        if (!valuesEqual(actual.at(i), expected.at(i), mode, false))
            return false;
    }
    return true;
}

QList<LcValue> sortedByKey(const QList<LcValue> &values, bool deep)
{
    QList<LcValue> sorted = values;
    std::stable_sort(sorted.begin(), sorted.end(), [deep](const LcValue &a, const LcValue &b) {
        return canonicalKey(a, deep) < canonicalKey(b, deep);
    });
    return sorted;
}

bool multisetEqual(const QList<LcValue> &actual, const QList<LcValue> &expected, ComparisonMode mode)
{
    if (actual.size() != expected.size())
        return false;

    const bool deep = mode == ComparisonMode::UnorderedDeep;
    if (arraysEqual(sortedByKey(actual, deep), sortedByKey(expected, deep), mode))
        return true;

    // Floats within tolerance need not sort alike, so fall back to greedy matching.
    QList<LcValue> remaining = expected;
    for (const LcValue &item : actual) {
        const auto match = std::find_if(remaining.cbegin(), remaining.cend(), [&](const LcValue &candidate) {
            return valuesEqual(item, candidate, mode, false);
        });
        if (match == remaining.cend())
            return false;
        remaining.removeAt(match - remaining.cbegin());
    }
    return true;
}

bool objectsEqual(const LcValue &actual, const LcValue &expected, ComparisonMode mode)
{
    // An in-place prefix output: compare only the first k elements of the other entries.
    const LcValue *result = expected.entry(QStringLiteral("return"));
    const bool hasPrefix = result && result->isNumber() && lcIsIntegerText(result->text);
    const qsizetype prefix = hasPrefix ? result->text.toLongLong() : 0;

    for (const auto &entry : expected.entries) {
        const LcValue *actualValue = actual.entry(entry.first);
        if (!actualValue)
            return false;

        const bool isReturn = entry.first == u"return";
        if (!isReturn && hasPrefix && entry.second.isArray() && actualValue->isArray()) {
            const LcValue a = LcValue::makeArray(actualValue->items.first(std::min(prefix, actualValue->items.size())));
            const LcValue e = LcValue::makeArray(entry.second.items.first(std::min(prefix, entry.second.items.size())));
            if (!valuesEqual(a, e, mode, true))
                return false;
            continue;
        }
        if (!valuesEqual(*actualValue, entry.second, mode, !isReturn))
            return false;
    }
    return true;
}

bool scalarsEqual(const LcValue &actual, const LcValue &expected)
{
    using Kind = LcValue::Kind;
    const bool actualIsText = actual.kind == Kind::String || actual.kind == Kind::Symbol;
    switch (expected.kind) {
    case Kind::Null:
        // LeetCode prints empty lists and trees as [] and null interchangeably in some problems.
        return actual.isNull() || (actual.isArray() && actual.items.isEmpty());
    case Kind::Bool: return actual.kind == Kind::Bool && actual.boolean == expected.boolean;
    case Kind::Number: return actual.isNumber() && numbersEqual(actual.text, expected.text);
    case Kind::String:
    case Kind::Symbol: return actualIsText && actual.text == expected.text;
    case Kind::Array: return actual.isNull() && expected.items.isEmpty();
    case Kind::Object: return false;
    }
    return false;
}

bool valuesEqual(const LcValue &actual, const LcValue &expected, ComparisonMode mode, bool topLevel)
{
    if (expected.kind == LcValue::Kind::Symbol && expected.text == u"_")
        return true;
    if (expected.isObject() && actual.isObject())
        return objectsEqual(actual, expected, mode);
    if (expected.isArray() && actual.isArray()) {
        const bool unordered = mode == ComparisonMode::UnorderedDeep
                               || (mode == ComparisonMode::Unordered && topLevel);
        return unordered ? multisetEqual(actual.items, expected.items, mode)
                         : arraysEqual(actual.items, expected.items, mode);
    }
    return scalarsEqual(actual, expected);
}

} // namespace

namespace OutputComparator {

ComparisonResult compare(const LcValue &actual, const LcValue &expected, ComparisonMode mode)
{
    if (valuesEqual(actual, expected, mode, true))
        return {true, {}};

    if (mode == ComparisonMode::Exact && valuesEqual(actual, expected, ComparisonMode::Unordered, true)) {
        return {false, QStringLiteral("The output matches if the order of the list is ignored. If this "
                                      "problem accepts any order, set \"comparison\": \"unordered\" in "
                                      "metadata.json.")};
    }
    if (mode != ComparisonMode::UnorderedDeep
        && valuesEqual(actual, expected, ComparisonMode::UnorderedDeep, true)) {
        return {false, QStringLiteral("The output matches if order is ignored at every level. If this "
                                      "problem accepts any order, set \"comparison\": \"unordered-deep\" "
                                      "in metadata.json.")};
    }
    return {false, {}};
}

ComparisonMode detectMode(const QString &slug, const ProblemMeta &meta, const QString &contentHtml)
{
    static const QHash<QString, ComparisonMode> known = {
        {QStringLiteral("group-anagrams"), ComparisonMode::UnorderedDeep},
        {QStringLiteral("3sum"), ComparisonMode::UnorderedDeep},
        {QStringLiteral("4sum"), ComparisonMode::UnorderedDeep},
        {QStringLiteral("subsets"), ComparisonMode::UnorderedDeep},
        {QStringLiteral("subsets-ii"), ComparisonMode::UnorderedDeep},
        {QStringLiteral("combination-sum"), ComparisonMode::UnorderedDeep},
        {QStringLiteral("combination-sum-ii"), ComparisonMode::UnorderedDeep},
        {QStringLiteral("combination-sum-iii"), ComparisonMode::UnorderedDeep},
        {QStringLiteral("combinations"), ComparisonMode::UnorderedDeep},
        {QStringLiteral("permutations"), ComparisonMode::Unordered},
        {QStringLiteral("permutations-ii"), ComparisonMode::Unordered},
        {QStringLiteral("find-all-anagrams-in-a-string"), ComparisonMode::Unordered},
        {QStringLiteral("top-k-frequent-elements"), ComparisonMode::Unordered},
        {QStringLiteral("top-k-frequent-words"), ComparisonMode::Exact},
        {QStringLiteral("two-sum"), ComparisonMode::Unordered},
    };

    const auto found = known.constFind(slug);
    if (found != known.constEnd())
        return *found;

    // Only list outputs, returned or modified in place, can be unordered.
    const bool producesList = !HarnessGenerator::metaElementType(meta.returnType).isEmpty() || meta.hasOutput;
    static const QRegularExpression anyOrder(
        QStringLiteral(R"(\bin any order\b|\border (?:of the (?:output|answer|result|elements) )?(?:does not matter|may be changed)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    if (producesList && anyOrder.match(QuestionParser::htmlToText(contentHtml)).hasMatch())
        return ComparisonMode::Unordered;
    return ComparisonMode::Exact;
}

QString modeName(ComparisonMode mode)
{
    switch (mode) {
    case ComparisonMode::Unordered: return QStringLiteral("unordered");
    case ComparisonMode::UnorderedDeep: return QStringLiteral("unordered-deep");
    case ComparisonMode::Exact: break;
    }
    return QStringLiteral("exact");
}

std::optional<ComparisonMode> modeFromName(const QString &name)
{
    for (const ComparisonMode mode : {ComparisonMode::Exact, ComparisonMode::Unordered, ComparisonMode::UnorderedDeep}) {
        if (modeName(mode) == name)
            return mode;
    }
    return std::nullopt;
}

ParsedHarnessOutput parseHarnessOutput(const QString &output, const QString &nonce)
{
    const QString marker = QStringLiteral("@@LCR:%1:").arg(nonce);
    ParsedHarnessOutput parsed;
    QStringList kept;

    const QStringList lines = output.split(u'\n');
    for (QString line : lines) {
        if (line.endsWith(u'\r'))
            line.chop(1);
        if (!line.startsWith(marker)) {
            kept << line;
            continue;
        }

        // A line cut short by a kill simply fails to parse and is skipped.
        const QJsonObject object = QJsonDocument::fromJson(line.mid(marker.size()).toUtf8()).object();
        if (!object.isEmpty()) {
            HarnessRecord record;
            record.event = object.value(QLatin1String("event")).toString();
            record.id = object.value(QLatin1String("id")).toInt();
            record.status = object.value(QLatin1String("status")).toString();
            record.error = object.value(QLatin1String("error")).toString();
            if (object.contains(QLatin1String("output")))
                record.output = object.value(QLatin1String("output")).toString();
            if (object.contains(QLatin1String("expected")))
                record.expected = object.value(QLatin1String("expected")).toString();
            if (object.contains(QLatin1String("timeUs")))
                record.timeUs = object.value(QLatin1String("timeUs")).toDouble();
            parsed.records.append(std::move(record));
        }

        // The harness writes \n before each record; that blank line goes too.
        if (!kept.isEmpty() && kept.last().isEmpty())
            kept.removeLast();
    }

    parsed.userStdout = kept.join(QLatin1Char('\n'));
    static const QRegularExpression leadingNewlines(QStringLiteral(R"(^\n+)"));
    parsed.userStdout.remove(leadingNewlines);
    return parsed;
}

QString describeCrash(int exitCode, int signal)
{
    switch (signal) {
    case SIGSEGV:
        return QStringLiteral("SIGSEGV: Segmentation fault (invalid memory access: null pointer, "
                              "out-of-bounds index, or stack overflow from deep recursion)");
    case SIGABRT:
        return QStringLiteral("SIGABRT: Aborted (failed assertion, std::terminate, or memory "
                              "corruption detected)");
    case SIGFPE:
        return QStringLiteral("SIGFPE: Floating point exception (integer division or modulo by zero)");
    case SIGBUS: return QStringLiteral("SIGBUS: Bus error (invalid memory access)");
    case SIGILL:
        return QStringLiteral("SIGILL: Illegal instruction (often a non-void function that returned "
                              "no value)");
    case SIGKILL: return QStringLiteral("SIGKILL: Killed");
    case 0: break;
    default: return QStringLiteral("Terminated by signal %1").arg(signal);
    }
    if (exitCode > 128 && exitCode < 160)
        return QStringLiteral("Terminated by signal %1 (exit code %2)").arg(exitCode - 128).arg(exitCode);
    return QStringLiteral("Exited with code %1").arg(exitCode);
}

} // namespace OutputComparator
