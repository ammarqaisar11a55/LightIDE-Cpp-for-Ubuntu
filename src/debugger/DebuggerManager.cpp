#include "debugger/DebuggerManager.h"

#include "execution/PtyChannel.h"

#include <QProcess>

#include <utility>

namespace {

constexpr int exitGraceMs = 2000;

QString describeStop(const MiRecord &record)
{
    const QString reason = record.payload.text(QStringLiteral("reason"));
    if (reason == QLatin1String("breakpoint-hit"))
        return DebuggerManager::tr("Breakpoint %1").arg(record.payload.text(QStringLiteral("bkptno")));
    if (reason == QLatin1String("signal-received")) {
        return DebuggerManager::tr("Received %1 (%2)")
            .arg(record.payload.text(QStringLiteral("signal-name")),
                 record.payload.text(QStringLiteral("signal-meaning")));
    }
    if (reason == QLatin1String("end-stepping-range"))
        return DebuggerManager::tr("Step finished");
    if (reason == QLatin1String("function-finished"))
        return DebuggerManager::tr("Returned from function");
    return DebuggerManager::tr("Paused");
}

StackFrame frameFrom(const MiValue &frame)
{
    StackFrame result;
    result.level = frame.text(QStringLiteral("level")).toInt();
    result.function = frame.text(QStringLiteral("func"));
    result.file = frame.text(QStringLiteral("fullname"));
    result.line = frame.text(QStringLiteral("line")).toInt();
    result.address = frame.text(QStringLiteral("addr"));
    return result;
}

} // namespace

DebuggerManager::DebuggerManager(QObject *parent)
    : QObject(parent)
    , m_inferiorPty(new PtyChannel(this))
{
    connect(m_inferiorPty, &PtyChannel::dataReady, this, &DebuggerManager::inferiorOutput);

    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_gdb)
            m_gdb->kill();
    });
}

DebuggerManager::~DebuggerManager()
{
    if (m_gdb) {
        m_gdb->disconnect(this);
        m_gdb->kill();
        m_gdb->waitForFinished(500);
    }
}

void DebuggerManager::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void DebuggerManager::start(const DebugRequest &request)
{
    if (isActive())
        return;

    QString error;
    if (!m_inferiorPty->open(&error)) {
        emit errorOccurred(tr("Could not allocate a terminal: %1").arg(error));
        return;
    }

    m_request = request;
    m_buffer.clear();
    m_callbacks.clear();
    m_breakpoints.clear();
    m_pendingAdds.clear();
    m_pendingRemovals.clear();
    m_frames.clear();
    m_programExited = false;
    setState(State::Starting);

    m_gdb = new QProcess(this);
    m_gdb->setProcessChannelMode(QProcess::MergedChannels);
    if (!request.workingDirectory.isEmpty())
        m_gdb->setWorkingDirectory(request.workingDirectory);

    connect(m_gdb, &QProcess::started, this, &DebuggerManager::onStarted);
    connect(m_gdb, &QProcess::readyRead, this, &DebuggerManager::onReadable);
    connect(m_gdb, &QProcess::finished, this, &DebuggerManager::onFinished);
    connect(m_gdb, &QProcess::errorOccurred, this, [this](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart)
            return;
        emit errorOccurred(tr("Could not start GDB: %1").arg(m_gdb->errorString()));
        onFinished();
    });

    m_gdb->start(request.gdbPath, {QStringLiteral("--interpreter=mi3"), QStringLiteral("--nx"),
                                   QStringLiteral("--quiet")});
}

void DebuggerManager::onStarted()
{
    send(QStringLiteral("-gdb-set confirm off"));
    send(QStringLiteral("-gdb-set pagination off"));
    send(QStringLiteral("-gdb-set width 0"));
    send(QStringLiteral("-gdb-set height 0"));
    // Keeps GDB responsive to Stop and to breakpoints while the program runs.
    send(QStringLiteral("-gdb-set mi-async on"));
    // Debug sessions must never reach for the network.
    send(QStringLiteral("-gdb-set debuginfod enabled off"));
    // Step Into should enter the user's code, not libstdc++ internals. This
    // matches functions because GDB's file globs (skip -gfi) also match relative
    // source names, which would skip the user's own code.
    send(QStringLiteral("-interpreter-exec console \"skip -rfu ^(std|__gnu_cxx|__cxxabiv1)::\""));

    send(QStringLiteral("-file-exec-and-symbols %1").arg(MiParser::quote(m_request.program)),
         [this](const MiRecord &record) {
             if (record.resultClass == QLatin1String("error")) {
                 emit errorOccurred(record.payload.text(QStringLiteral("msg")));
                 stop();
             }
         });
    send(QStringLiteral("-inferior-tty-set %1").arg(MiParser::quote(m_inferiorPty->slaveName())));
    if (!m_request.workingDirectory.isEmpty()) {
        send(QStringLiteral("-environment-cd %1").arg(MiParser::quote(m_request.workingDirectory)));
    }
    if (!m_request.arguments.isEmpty())
        send(QStringLiteral("-exec-arguments %1").arg(m_request.arguments));

    for (const SourceLocation &location : std::as_const(m_request.breakpoints))
        insertBreakpoint(location);

    // Without breakpoints the program would simply run to the end, so a first
    // press of Debug stops at main instead.
    send(m_request.breakpoints.isEmpty() ? QStringLiteral("-exec-run --start")
                                         : QStringLiteral("-exec-run"),
         [this](const MiRecord &record) {
             if (record.resultClass == QLatin1String("error")) {
                 emit errorOccurred(record.payload.text(QStringLiteral("msg")));
                 stop();
             }
         });
}

void DebuggerManager::send(const QString &command, Callback callback)
{
    if (!m_gdb)
        return;
    const int token = m_nextToken++;
    if (callback)
        m_callbacks.insert(token, std::move(callback));
    m_gdb->write(QByteArray::number(token) + command.toUtf8() + '\n');
}

void DebuggerManager::onReadable()
{
    m_buffer.append(m_gdb->readAll());

    qsizetype newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (!line.isEmpty())
            handleRecord(MiParser::parse(line));
    }
}

void DebuggerManager::handleRecord(const MiRecord &record)
{
    switch (record.type) {
    case MiRecord::Type::Result: {
        const Callback callback = m_callbacks.take(record.token);
        if (callback) {
            callback(record);
        } else if (record.resultClass == QLatin1String("error")) {
            emit consoleOutput(record.payload.text(QStringLiteral("msg")) + QLatin1Char('\n'));
        }
        break;
    }
    case MiRecord::Type::ExecAsync:
        if (record.resultClass == QLatin1String("running")) {
            setState(State::Running);
            emit resumed();
        } else if (record.resultClass == QLatin1String("stopped")) {
            handleStopped(record);
        }
        break;
    case MiRecord::Type::ConsoleStream:
    case MiRecord::Type::TargetStream:
    case MiRecord::Type::LogStream:
        emit consoleOutput(record.stream);
        break;
    default:
        break;
    }
}

void DebuggerManager::handleStopped(const MiRecord &record)
{
    if (m_state == State::Stopping)
        return;

    const QString reason = record.payload.text(QStringLiteral("reason"));
    if (reason.startsWith(QLatin1String("exited"))) {
        m_programExited = true;
        // Exit codes are reported in octal, without one meaning zero.
        const QString code = record.payload.text(QStringLiteral("exit-code"));
        m_inferiorPty->drain();
        emit programExited(code.isEmpty() ? 0 : code.toInt(nullptr, 8));
        stop();
        return;
    }

    setState(State::Paused);
    flushBreakpointChanges();

    StackFrame top;
    if (const MiValue *frame = record.payload.find(QStringLiteral("frame")))
        top = frameFrom(*frame);
    emit paused(top.file, top.line, describeStop(record));
    refreshStack();
}

void DebuggerManager::refreshStack()
{
    send(QStringLiteral("-stack-list-frames"), [this](const MiRecord &record) {
        m_frames.clear();
        if (const MiValue *stack = record.payload.find(QStringLiteral("stack"))) {
            const QList<MiValue> frames = stack->items();
            for (const MiValue &frame : frames)
                m_frames.append(frameFrom(frame));
        }
        emit framesChanged(m_frames);
        refreshVariables();
    });
}

void DebuggerManager::refreshVariables()
{
    // --simple-values gives types but omits the values of arrays and structs,
    // and --all-values gives every value but no types, so both are merged.
    send(QStringLiteral("-stack-list-variables --simple-values"), [this](const MiRecord &types) {
        QHash<QString, QString> typeOf;
        if (const MiValue *list = types.payload.find(QStringLiteral("variables"))) {
            const QList<MiValue> entries = list->items();
            for (const MiValue &entry : entries) {
                typeOf.insert(entry.text(QStringLiteral("name")),
                              entry.text(QStringLiteral("type")));
            }
        }

        send(QStringLiteral("-stack-list-variables --all-values"),
             [this, typeOf](const MiRecord &values) {
                 QList<Variable> variables;
                 if (const MiValue *list = values.payload.find(QStringLiteral("variables"))) {
                     const QList<MiValue> entries = list->items();
                     for (const MiValue &entry : entries) {
                         Variable variable;
                         variable.name = entry.text(QStringLiteral("name"));
                         variable.type = typeOf.value(variable.name);
                         variable.value = entry.text(QStringLiteral("value"));
                         variable.isArgument = entry.text(QStringLiteral("arg")) == QLatin1String("1");
                         variables.append(variable);
                     }
                 }
                 emit variablesChanged(variables);
             });
    });
}

void DebuggerManager::continueExecution()
{
    if (m_state == State::Paused)
        send(QStringLiteral("-exec-continue"));
}

void DebuggerManager::stepInto()
{
    if (m_state == State::Paused)
        send(QStringLiteral("-exec-step"));
}

void DebuggerManager::stepOver()
{
    if (m_state == State::Paused)
        send(QStringLiteral("-exec-next"));
}

void DebuggerManager::stepOut()
{
    if (m_state == State::Paused) {
        send(QStringLiteral("-exec-finish"), [this](const MiRecord &record) {
            if (record.resultClass == QLatin1String("error"))
                emit consoleOutput(record.payload.text(QStringLiteral("msg")) + QLatin1Char('\n'));
        });
    }
}

void DebuggerManager::interrupt()
{
    if (m_state == State::Running)
        send(QStringLiteral("-exec-interrupt"));
}

void DebuggerManager::insertBreakpoint(const SourceLocation &location)
{
    send(QStringLiteral("-break-insert -f --source %1 --line %2")
             .arg(MiParser::quote(location.file))
             .arg(location.line),
         [this, location](const MiRecord &record) {
             if (record.resultClass == QLatin1String("error")) {
                 emit consoleOutput(record.payload.text(QStringLiteral("msg")) + QLatin1Char('\n'));
                 return;
             }
             if (const MiValue *breakpoint = record.payload.find(QStringLiteral("bkpt"))) {
                 m_breakpoints.append({location, breakpoint->text(QStringLiteral("number")).toInt()});
             }
         });
}

void DebuggerManager::addBreakpoint(const SourceLocation &location)
{
    if (m_state == State::Paused)
        insertBreakpoint(location);
    else if (m_state == State::Running || m_state == State::Starting)
        m_pendingAdds.append(location);
}

void DebuggerManager::removeBreakpoint(const SourceLocation &location)
{
    if (m_pendingAdds.removeOne(location))
        return;
    if (m_state == State::Running || m_state == State::Starting) {
        m_pendingRemovals.append(location);
        return;
    }
    for (int i = 0; i < m_breakpoints.size(); ++i) {
        if (m_breakpoints.at(i).first == location) {
            send(QStringLiteral("-break-delete %1").arg(m_breakpoints.at(i).second));
            m_breakpoints.removeAt(i);
            return;
        }
    }
}

// GDB in all-stop mode refuses breakpoint changes while the program runs, so
// edits made then are applied at the next stop.
void DebuggerManager::flushBreakpointChanges()
{
    const QList<SourceLocation> removals = std::exchange(m_pendingRemovals, {});
    for (const SourceLocation &location : removals)
        removeBreakpoint(location);
    const QList<SourceLocation> additions = std::exchange(m_pendingAdds, {});
    for (const SourceLocation &location : additions)
        insertBreakpoint(location);
}

void DebuggerManager::selectFrame(int level)
{
    if (m_state != State::Paused || level < 0 || level >= m_frames.size())
        return;
    const StackFrame frame = m_frames.at(level);
    send(QStringLiteral("-stack-select-frame %1").arg(level), [this, frame](const MiRecord &record) {
        if (record.resultClass == QLatin1String("error"))
            return;
        emit frameSelected(frame.file, frame.line);
        refreshVariables();
    });
}

void DebuggerManager::evaluate(const QString &expression)
{
    send(QStringLiteral("-data-evaluate-expression %1").arg(MiParser::quote(expression)),
         [this, expression](const MiRecord &record) {
             const QString text = record.resultClass == QLatin1String("error")
                                      ? record.payload.text(QStringLiteral("msg"))
                                      : record.payload.text(QStringLiteral("value"));
             emit consoleOutput(QStringLiteral("%1 = %2\n").arg(expression, text));
         });
}

void DebuggerManager::runConsoleCommand(const QString &command)
{
    send(QStringLiteral("-interpreter-exec console %1").arg(MiParser::quote(command)));
}

void DebuggerManager::writeToProgram(const QByteArray &data)
{
    m_inferiorPty->write(data);
}

void DebuggerManager::stop()
{
    if (!m_gdb || m_state == State::Stopping)
        return;
    setState(State::Stopping);
    send(QStringLiteral("-gdb-exit"));
    m_killTimer.start(exitGraceMs);
}

void DebuggerManager::onFinished()
{
    if (!m_gdb)
        return;
    m_killTimer.stop();
    m_gdb->deleteLater();
    m_gdb = nullptr;
    m_callbacks.clear();
    m_inferiorPty->drain();
    m_inferiorPty->close();
    setState(State::Idle);
    emit sessionEnded();
}
