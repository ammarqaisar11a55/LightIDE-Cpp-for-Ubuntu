#pragma once

#include "leetcode/ProblemFolder.h"

#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <optional>

class QProcess;

enum class LcTestStatus { Pass, Fail, RuntimeError, Timeout, OutputLimit, NoExpected };

struct LcTestResult
{
    int id = 0;
    QString name;
    LcTestStatus status = LcTestStatus::RuntimeError;
    QString input;
    std::optional<QString> expected;
    std::optional<QString> actual;
    std::optional<double> timeMs;
    QString stdoutText;
    QString stderrText;
    QString message;
    QString hint;
};

struct LcRunReport
{
    QList<LcTestResult> tests;
    int passed = 0;
    int total = 0;
    double totalTimeMs = 0;
    bool failed = false;   // A test failed, as distinct from tests with no expected output.
    QString error;         // Set when the program failed before any test ran.
};

// Runs a compiled harness. Each test gets its own process, so that a crash, an
// infinite loop or a corrupted heap in one test cannot affect the others.
class TestSuiteRunner : public QObject
{
    Q_OBJECT

public:
    explicit TestSuiteRunner(QObject *parent = nullptr);
    ~TestSuiteRunner() override;

    // onlyTests limits the run; empty runs everything the harness defines.
    void start(const ProblemFolder &folder, const ProblemMetadata &metadata,
               const QList<LcTestCase> &tests, const LeetCodeRunSettings &settings,
               const QList<int> &onlyTests = {});
    void cancel();
    bool isRunning() const { return m_process != nullptr; }

signals:
    void testFinished(const LcTestResult &result);
    void finished(const LcRunReport &report);

private:
    enum class Phase { Listing, Running };

    void launch(const QStringList &arguments);
    void onProcessFinished(int exitCode, int signal, bool crashed);
    void onListed(const QString &output, int exitCode, int signal, const QString &errors);
    void runNext();
    LcTestResult evaluate(int id, const QString &output, const QString &errors, int exitCode, int signal);
    void complete();

    ProblemFolder m_folder;
    ProblemMetadata m_metadata;
    QList<LcTestCase> m_tests;
    LeetCodeRunSettings m_settings;
    QList<int> m_only;

    QProcess *m_process = nullptr;
    QTimer m_timeout;
    Phase m_phase = Phase::Listing;
    QByteArray m_output;
    QByteArray m_errors;
    QString m_nonce;
    QList<int> m_queue;
    int m_currentId = 0;
    bool m_timedOut = false;
    bool m_overLimit = false;
    bool m_cancelled = false;
    LcRunReport m_report;
};
