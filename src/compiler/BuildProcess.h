#pragma once

#include "compiler/CompileCommand.h"
#include "compiler/Diagnostic.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QStringList>

#include <optional>

class QProcess;

struct BuildJob
{
    CommandLine command;
    QString label;   // Short description, such as the source file's name.
};

// One compile stage that may run in parallel, then an optional link. A
// single-file build is one compile job and no link.
struct BuildPlan
{
    QList<BuildJob> compileJobs;
    std::optional<BuildJob> linkJob;
    QString workingDirectory;
    QString outputPath;
    QStringList directories;   // Created before the first job runs.
};

struct BuildResult
{
    bool success = false;
    bool cancelled = false;
    bool upToDate = false;    // Nothing needed rebuilding.
    int exitCode = 0;
    qint64 durationMs = 0;
    int errors = 0;
    int warnings = 0;
    QString outputPath;
    QString failureMessage;   // Set when a tool could not be started at all.
};

class BuildProcess : public QObject
{
    Q_OBJECT

public:
    explicit BuildProcess(QObject *parent = nullptr);

    // maxParallel of 0 uses one job per CPU thread.
    void start(const BuildPlan &plan, int maxParallel = 0);
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void commandStarted(const QString &commandLine);
    void outputLine(const QString &line);
    void diagnosticFound(const Diagnostic &diagnostic);
    void finished(const BuildResult &result);

private:
    void launchNext();
    void launch(const BuildJob &job);
    void jobFinished(QProcess *process, int exitCode, bool startFailed);
    void complete();

    BuildPlan m_plan;
    QList<BuildJob> m_queue;
    QList<QProcess *> m_active;
    QElapsedTimer m_timer;
    BuildResult m_result;
    int m_maxParallel = 1;
    bool m_running = false;
    bool m_failed = false;
    bool m_linkStarted = false;
};
