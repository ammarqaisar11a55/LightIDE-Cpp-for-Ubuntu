#include "compiler/BuildProcess.h"

#include "compiler/DiagnosticsParser.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace {

constexpr int cancelGraceMs = 1000;

} // namespace

BuildProcess::BuildProcess(QObject *parent)
    : QObject(parent)
{
}

void BuildProcess::start(const BuildPlan &plan, int maxParallel)
{
    if (m_running)
        return;

    m_plan = plan;
    m_queue = plan.compileJobs;
    m_result = BuildResult{};
    m_result.outputPath = plan.outputPath;
    m_failed = false;
    m_linkStarted = false;
    m_running = true;
    m_maxParallel = maxParallel > 0 ? maxParallel : std::max(1, QThread::idealThreadCount());
    m_timer.start();

    for (const QString &directory : plan.directories)
        QDir().mkpath(directory);

    if (plan.compileJobs.isEmpty() && !plan.linkJob) {
        // Stay asynchronous so that callers see the same signal order either way.
        m_result.upToDate = true;
        QTimer::singleShot(0, this, &BuildProcess::complete);
        return;
    }
    launchNext();
}

void BuildProcess::cancel()
{
    if (!m_running)
        return;

    m_result.cancelled = true;
    m_failed = true;
    m_queue.clear();
    if (m_active.isEmpty()) {
        QTimer::singleShot(0, this, &BuildProcess::complete);
        return;
    }

    // The g++ driver forwards SIGTERM to cc1plus and removes its temporary files.
    const QList<QProcess *> active = m_active;
    for (QProcess *process : active)
        process->terminate();
    QTimer::singleShot(cancelGraceMs, this, [this] {
        const QList<QProcess *> remaining = m_active;
        for (QProcess *process : remaining)
            process->kill();
    });
}

void BuildProcess::launchNext()
{
    while (!m_failed && !m_queue.isEmpty() && m_active.size() < m_maxParallel)
        launch(m_queue.takeFirst());

    if (!m_active.isEmpty())
        return;

    if (!m_failed && m_plan.linkJob && !m_linkStarted) {
        m_linkStarted = true;
        launch(*m_plan.linkJob);
        return;
    }
    complete();
}

void BuildProcess::launch(const BuildJob &job)
{
    emit commandStarted(job.command.display());

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    if (!m_plan.workingDirectory.isEmpty())
        process->setWorkingDirectory(m_plan.workingDirectory);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process->setProcessEnvironment(environment);

    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                jobFinished(process, status == QProcess::NormalExit ? exitCode : -1, false);
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, program = job.command.program](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                m_result.failureMessage =
                    tr("Could not start \"%1\": %2").arg(program, process->errorString());
                jobFinished(process, -1, true);
            });

    m_active.append(process);
    process->start(job.command.program, job.command.arguments);
}

void BuildProcess::jobFinished(QProcess *process, int exitCode, bool startFailed)
{
    if (!m_active.removeOne(process))
        return;

    // Output is released whole per job so that parallel compiles do not interleave.
    const QString output = QString::fromUtf8(process->readAll());
    process->deleteLater();

    const auto lines = QStringView(output).split(u'\n');
    for (QStringView view : lines) {
        if (view.isEmpty())
            continue;
        const QString line = view.toString();
        emit outputLine(line);
        if (const auto diagnostic = DiagnosticsParser::parseLine(line)) {
            if (diagnostic->severity == Diagnostic::Severity::Error)
                ++m_result.errors;
            else if (diagnostic->severity == Diagnostic::Severity::Warning)
                ++m_result.warnings;
            emit diagnosticFound(*diagnostic);
        }
    }

    if (startFailed || exitCode != 0) {
        m_failed = true;
        if (m_result.exitCode == 0)
            m_result.exitCode = exitCode;
    }
    launchNext();
}

void BuildProcess::complete()
{
    if (!m_running)
        return;
    m_running = false;
    m_result.success = !m_failed;
    m_result.durationMs = m_timer.elapsed();
    emit finished(m_result);
}
