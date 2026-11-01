#pragma once

#include "compiler/BuildProcess.h"
#include "leetcode/HarnessGenerator.h"
#include "leetcode/LcProblem.h"
#include "leetcode/OutputComparator.h"

#include <QString>
#include <QStringList>

// The contents of metadata.json. It holds problem information only, never
// credentials. meta and starterCode are kept so that the harness can be
// regenerated offline and signature changes noticed on refresh.
struct ProblemMetadata
{
    int id = 0;
    QString questionId;
    QString slug;
    QString title;
    QString difficulty;
    QString url;
    QString support = QStringLiteral("function");   // function, design or manual.
    QString supportReason;
    bool manualHarness = false;   // The harness section belongs to the user.
    ComparisonMode comparison = ComparisonMode::Exact;
    ProblemMeta meta;
    QString starterCode;
    QString signatureHash;
    QString fetchedAt;
    QString generator = QStringLiteral("leetrunner");
    QString generatorVersion;
};

struct LeetCodeRunSettings
{
    QString compiler = QStringLiteral("g++");
    QString standard = QStringLiteral("c++17");
    QStringList flags = {QStringLiteral("-O2"), QStringLiteral("-Wall")};
    bool sanitizers = false;
    int timeoutMs = 5000;
};

// One problem on disk, in the layout the LeetRunner extension also uses:
//
//   <slug>/solution.cpp, metadata.json, tests.json
//   <slug>/.lc/leetcode.hpp, description.html, build output
class ProblemFolder
{
public:
    explicit ProblemFolder(const QString &directory = QString());

    QString directory() const { return m_directory; }
    QString solutionPath() const;
    QString metadataPath() const;
    QString testsPath() const;
    QString supportDirectory() const;
    QString headerPath() const;
    QString descriptionPath() const;
    QString binaryPath(bool debug = false) const;

    bool isProblem() const;

    ProblemMetadata readMetadata() const;
    void writeMetadata(const ProblemMetadata &metadata) const;
    QList<LcTestCase> readTests(const ProblemMetadata &metadata) const;
    void writeTests(const ProblemMetadata &metadata, const QList<LcTestCase> &tests) const;

    // Writes the folder for a fetched problem. An existing solution.cpp is kept
    // unless overwrite is set, in which case it is backed up into .lc first.
    // Returns warnings for the user.
    QStringList create(const LeetCodeProblem &problem, bool overwrite) const;

    // Updates everything except solution.cpp, keeping custom tests and the
    // user's comparison choice. Returns whether the signature changed.
    bool refresh(const LeetCodeProblem &problem) const;

    // Writes .lc/leetcode.hpp if it changed, and returns the plan to compile
    // solution.cpp. The plan is empty when the existing binary is current.
    BuildPlan makeBuildPlan(const ProblemMetadata &metadata, const LeetCodeRunSettings &settings, bool debug) const;

    // Records a successful build, for makeBuildPlan to compare against.
    void markBuilt(const LeetCodeRunSettings &settings, bool debug) const;

    // The directory for a file if it is a problem's solution.cpp, else empty.
    static QString folderOfSolution(const QString &filePath);

private:
    QStringList compileArguments(const LeetCodeRunSettings &settings, bool debug) const;
    QString buildHash(const LeetCodeRunSettings &settings, bool debug) const;

    QString m_directory;
};

namespace LeetCodeBuilder {

ProblemMetadata buildMetadata(const LeetCodeProblem &problem);
QString buildHeader(const ProblemMetadata &metadata);

// The harness runtime shipped with the IDE.
QString runtimeSource();

// source with its harness brought in line with tests. Manual harnesses are left alone.
QString syncHarness(const QString &source, const ProblemMetadata &metadata,
                    const QList<LcTestCase> &tests, QStringList *warnings = nullptr);

// Replaces example tests with fresh ones, keeping custom tests after them.
QList<LcTestCase> mergeExamples(const QList<LcTestCase> &existing, const QList<LcTestCase> &examples);

QString signatureHash(const QString &starterCode);

// tests.json as text, and back.
QString serializeTests(const QList<LcTestCase> &tests, const QStringList &keys);
QList<LcTestCase> parseTests(const QString &text, const QStringList &keys);

} // namespace LeetCodeBuilder
