#pragma once

#include "leetcode/CppSignature.h"
#include "leetcode/LcProblem.h"

#include <QStringList>

// How the test harness drives a solution, decided by checking what the judge
// feeds in against what the starter code declares.
struct HarnessPlan
{
    enum class Kind {
        Function,   // A normal class Solution method; the harness is fully generated.
        Design,     // A class driven by operation and argument arrays; fully generated.
        Manual,     // A special judge; a skeleton is generated once and the user wires it.
    };

    Kind kind = Kind::Manual;
    QString className;
    QString reason;   // Why a problem is manual.

    bool hasMethod = false;
    CppMethod method;   // Function plans, and manual plans when one was found.

    bool hasOutput = false;   // In-place output.
    int outputParamIndex = -1;
    bool prefixFromReturn = false;

    bool hasConstructor = false;
    CppMethod constructorMethod;
    QList<CppMethod> methods;
};

struct GeneratedHarness
{
    QString code;   // Everything after the TEST HARNESS marker line.
    QStringList warnings;
};

namespace HarnessGenerator {

HarnessPlan planHarness(const ProblemMeta &meta, const CppStarter &starter);

GeneratedHarness generate(const HarnessPlan &plan, const ProblemMeta &meta,
                          const QList<LcTestCase> &tests, const QStringList &knownTypes);

// A C++ expression for value as type, or an LcError if it doesn't fit.
QString literal(const CppType &type, const LcValue &value);

QString cppStringLiteral(const QString &value);

// LeetCode's element type: integer[] and list<integer> both give integer. Empty if not a list.
QString metaElementType(const QString &metaType);

// nums = [2,7,11,15], target = 9
QString describeInputs(const QStringList &keys, const QList<LcValue> &inputs);

} // namespace HarnessGenerator
