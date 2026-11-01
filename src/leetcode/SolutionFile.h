#pragma once

#include "leetcode/CppSignature.h"

#include <QList>
#include <QString>

#include <optional>

// The layout of solution.cpp: the user's code sits between two marker lines,
// and everything after the second marker is generated.
namespace SolutionFile {

extern const QString userSolutionMarker;
extern const QString testHarnessMarker;

struct Sections
{
    QString preamble;
    QString solution;
    QString harness;
    qsizetype harnessStart = 0;
};

struct Template
{
    QString frontendId;
    QString title;
    QString difficulty;
    QString url;
    QString starterCode;
    QString harnessCode;
};

std::optional<Sections> split(const QString &source);

// Replaces the harness section. If its marker was deleted, a new one is added.
QString replaceHarness(const QString &source, const QString &generatedHarness);

// The text to paste into LeetCode: the user's section without local-only code.
QString extractSubmission(const QString &source);

// Removes #ifdef LC_LOCAL blocks, keeping any #else branch, and // @local lines.
QString stripLocalOnly(const QString &code);

QString generate(const Template &file);

// The types a problem needs: those LeetCode ships in comments, plus the
// standard ListNode and TreeNode if the code uses them without a definition.
QList<CommentedDefinition> requiredDefinitions(const CppStarter &starter);

// .lc/leetcode.hpp: the generic runtime followed by this problem's types.
QString generateHeader(const QString &runtimeSource, const QList<CommentedDefinition> &definitions);

} // namespace SolutionFile
