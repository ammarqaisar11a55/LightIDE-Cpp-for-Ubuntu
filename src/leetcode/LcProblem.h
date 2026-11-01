#pragma once

#include "leetcode/LcValue.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

struct MetaParam
{
    QString name;
    QString type;   // LeetCode's spelling: integer, integer[], list<list<string>>, TreeNode.
};

struct MetaMethod
{
    QString name;
    QList<MetaParam> params;
    QString returnType;
};

// LeetCode's metaData field: what the judge feeds in and expects back.
struct ProblemMeta
{
    QString name;   // Function name, or the class name for a design problem.
    QList<MetaParam> params;
    QString returnType;
    bool systemDesign = false;   // A class whose methods are called in sequence.
    bool manual = false;         // The judge uses inputs that don't match the signature.
    QList<MetaParam> constructorParams;
    QList<MetaMethod> methods;

    // In-place output: the judge inspects this parameter, not only the return value.
    bool hasOutput = false;
    int outputParamIndex = -1;
    QString outputSize;   // "ret" means only the first <return value> elements count.
};

enum class TestSource { Example, Custom };

// One test. For a design problem, inputs is the operations array and the
// arguments array, as in LeetCode's own format.
struct LcTestCase
{
    int id = 0;   // 1-based, used by the harness as "Test Case N".
    QString name;
    TestSource source = TestSource::Example;
    QList<LcValue> inputs;
    std::optional<LcValue> expected;
    QString expectedText;   // The raw statement text, kept when it could not be parsed.
};

struct LeetCodeProblem
{
    QString id;
    QString frontendId;
    QString slug;
    QString title;
    QString difficulty;
    bool isPaidOnly = false;
    QString url;
    QString contentHtml;
    QString starterCode;
    ProblemMeta meta;
    QList<LcTestCase> examples;
    QStringList warnings;
};
