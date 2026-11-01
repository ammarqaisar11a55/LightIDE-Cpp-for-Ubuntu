#include "compiler/DiagnosticsParser.h"

#include <QRegularExpression>

namespace {

Diagnostic::Severity severityFrom(QStringView word)
{
    if (word == u"note")
        return Diagnostic::Severity::Note;
    if (word == u"warning")
        return Diagnostic::Severity::Warning;
    return Diagnostic::Severity::Error;
}

std::optional<Diagnostic> parseLinkerLine(QString line)
{
    // "/usr/bin/ld: <rest>", also matching ld.gold and friends.
    static const QRegularExpression toolPrefix(QStringLiteral(R"(^\S*\bld(?:\.\w+)?: )"));
    const auto prefix = toolPrefix.match(line);
    const bool fromLinker = prefix.hasMatch();
    if (fromLinker)
        line.remove(0, prefix.capturedLength());

    // "main.cpp:5:(.text+0x9): undefined reference to `f()'"
    // "main.cpp:(.text+0x9): undefined reference to `f()'"
    static const QRegularExpression sectionRef(
        QStringLiteral(R"(^(.+?):(?:(\d+):)?\([^)]*\+0x[0-9a-fA-F]+\): (.*)$)"));
    const auto ref = sectionRef.match(line);
    if (ref.hasMatch()) {
        QString file = ref.captured(1);
        // ld may prefix the object: "/tmp/cc123.o:main.cpp".
        const int object = file.lastIndexOf(QLatin1String(".o:"));
        if (object >= 0)
            file.remove(0, object + 3);

        Diagnostic d;
        d.message = ref.captured(3);
        d.severity = d.message.startsWith(QLatin1String("warning:"))
                         ? Diagnostic::Severity::Warning
                         : Diagnostic::Severity::Error;
        d.file = file;
        d.line = ref.captured(2).toInt();
        return d;
    }

    if (!fromLinker)
        return std::nullopt;

    // "/usr/bin/ld: /tmp/cc123.o: in function `main':" only introduces the next line.
    if (line.contains(QLatin1String(": in function ")))
        return std::nullopt;

    Diagnostic d;
    if (line.startsWith(QLatin1String("warning: "))) {
        d.severity = Diagnostic::Severity::Warning;
        d.message = line.mid(9);
    } else {
        d.severity = Diagnostic::Severity::Error;
        d.message = line;
    }
    return d;
}

} // namespace

std::optional<Diagnostic> DiagnosticsParser::parseLine(const QString &line)
{
    // Everything before ":<line>[:<col>]: <severity>:" is the path, so spaces and
    // even colons inside a path are kept. The lazy match backtracks past any
    // earlier ":<digits>:" that is not followed by a severity.
    static const QRegularExpression located(QStringLiteral(
        R"(^(.+?):(\d+):(?:(\d+):)? (fatal error|error|warning|note): (.*)$)"));
    const auto match = located.match(line);
    if (match.hasMatch()) {
        Diagnostic d;
        d.file = match.captured(1);
        d.line = match.captured(2).toInt();
        d.column = match.captured(3).toInt();
        d.severity = severityFrom(match.capturedView(4));
        d.message = match.captured(5);
        return d;
    }

    // "cc1plus: fatal error: x.cpp: No such file or directory"
    // "collect2: error: ld returned 1 exit status"
    static const QRegularExpression unlocated(
        QStringLiteral(R"(^([^\s:]+): (fatal error|error|warning|note): (.*)$)"));
    const auto tool = unlocated.match(line);
    if (tool.hasMatch()) {
        Diagnostic d;
        d.severity = severityFrom(tool.capturedView(2));
        d.message = tool.captured(3);
        return d;
    }

    return parseLinkerLine(line);
}

QList<Diagnostic> DiagnosticsParser::parse(const QString &output)
{
    QList<Diagnostic> diagnostics;
    const auto lines = QStringView(output).split(u'\n');
    for (QStringView line : lines) {
        if (auto diagnostic = parseLine(line.toString()))
            diagnostics.append(*diagnostic);
    }
    return diagnostics;
}
