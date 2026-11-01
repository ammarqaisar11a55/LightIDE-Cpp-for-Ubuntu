#pragma once

#include "leetcode/LcProblem.h"

#include <QList>
#include <QString>

#include <optional>

enum class ComparisonMode {
    Exact,           // Structural equality; numbers compare numerically.
    Unordered,       // The top-level list may be in any order: Two Sum, Top K Frequent.
    UnorderedDeep,   // Lists at every level may be in any order: Group Anagrams, 3Sum.
};

struct ComparisonResult
{
    bool equal = false;
    QString hint;   // Shown on a failure, such as when only the order differs.
};

// A line the harness writes, in the form @@LCR:<nonce>:<json>.
struct HarnessRecord
{
    QString event;   // "result" or "test".
    int id = 0;
    QString status;  // "ok", "exception" or "not_wired".
    std::optional<QString> output;
    std::optional<QString> expected;
    std::optional<double> timeUs;
    QString error;
};

struct ParsedHarnessOutput
{
    QList<HarnessRecord> records;
    QString userStdout;   // Whatever the user's code printed.
};

namespace OutputComparator {

// In-place prefix outputs compare the first k elements only, and the symbol _
// in the expected value matches anything.
ComparisonResult compare(const LcValue &actual, const LcValue &expected, ComparisonMode mode);

// A known override, else unordered when the statement says the answer may be in
// any order and the output is a list, else exact.
ComparisonMode detectMode(const QString &slug, const ProblemMeta &meta, const QString &contentHtml);

QString modeName(ComparisonMode mode);
std::optional<ComparisonMode> modeFromName(const QString &name);

ParsedHarnessOutput parseHarnessOutput(const QString &output, const QString &nonce);

// A plain-language account of how a process crashed.
QString describeCrash(int exitCode, int signal);

} // namespace OutputComparator
