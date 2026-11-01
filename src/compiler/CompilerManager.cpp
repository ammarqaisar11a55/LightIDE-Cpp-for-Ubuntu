#include "compiler/CompilerManager.h"

#include "core/Settings.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace {

QString resolveTool(const QString &command)
{
    if (command.contains(QLatin1Char('/'))) {
        const QFileInfo info(command);
        return info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString();
    }
    return QStandardPaths::findExecutable(command);
}

} // namespace

CompilerManager::CompilerManager(QObject *parent)
    : QObject(parent)
{
}

CompilerManager::~CompilerManager()
{
    const QList<QProcess *> running = findChildren<QProcess *>();
    for (QProcess *process : running) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(200);
    }
}

void CompilerManager::detect(const CompilerSettings &settings)
{
    ++m_generation;   // Results from an earlier detect() are stale.
    m_pending = 0;
    probe(m_gcc, QStringLiteral("GCC"), settings.gccPath);
    probe(m_gxx, QStringLiteral("G++"), settings.gxxPath);
    probe(m_gdb, QStringLiteral("GDB"), settings.gdbPath);
    if (m_pending == 0)
        QTimer::singleShot(0, this, &CompilerManager::detectionFinished);
}

void CompilerManager::probe(ToolInfo &tool, const QString &name, const QString &command)
{
    tool = ToolInfo{};
    tool.name = name;
    tool.command = command;
    tool.path = resolveTool(command);
    tool.found = !tool.path.isEmpty();
    if (!tool.found)
        return;

    ++m_pending;
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);

    const int generation = m_generation;
    const auto done = [this, process, generation, tool = &tool](bool ran) {
        process->deleteLater();
        if (generation != m_generation)
            return;
        if (ran)
            tool->version = parseVersion(QString::fromUtf8(process->readAll()));
        else
            tool->found = false;
        if (--m_pending == 0)
            emit detectionFinished();
    };

    connect(process, &QProcess::finished, this, [done] { done(true); });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(false);
    });
    process->start(tool.path, {QStringLiteral("--version")});
}

QString CompilerManager::installHint()
{
    return QStringLiteral("sudo apt update\nsudo apt install build-essential gdb");
}

QString CompilerManager::parseVersion(const QString &versionOutput)
{
    const QString firstLine = versionOutput.section(QLatin1Char('\n'), 0, 0).trimmed();

    // "gcc (Ubuntu 13.2.0-23ubuntu4) 13.2.0" ends with the plain version, and
    // development builds of GDB append a suffix: "15.0.50.20240403-git".
    static const QRegularExpression trailing(QStringLiteral(R"((\d+\.\d+(?:\.\d+)*)(?:-\S+)?$)"));
    const auto match = trailing.match(firstLine);
    if (match.hasMatch())
        return match.captured(1);

    static const QRegularExpression anywhere(QStringLiteral(R"(\d+\.\d+(?:\.\d+)*)"));
    return anywhere.match(firstLine).captured(0);
}
