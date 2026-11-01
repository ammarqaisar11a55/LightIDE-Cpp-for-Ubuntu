#include "compiler/CompileCommand.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace {

QString shellQuote(const QString &argument)
{
    static const QRegularExpression safe(QStringLiteral(R"(^[A-Za-z0-9_@%+=:,./-]+$)"));
    if (safe.match(argument).hasMatch())
        return argument;
    QString quoted = argument;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

// Keeps output machine-readable; see DiagnosticsParser.
const QStringList diagnosticFlags = {QStringLiteral("-fdiagnostics-color=never"),
                                     QStringLiteral("-fdiagnostics-column-unit=byte"),
                                     QStringLiteral("-fmessage-length=0")};

QStringList compileFlags(const BuildOptions &options, SourceLanguage language)
{
    QStringList args;
    args << QStringLiteral("-std=%1")
                .arg(language == SourceLanguage::Cpp ? options.cxxStandard : options.cStandard);
    args << options.warningFlags;
    if (!options.optimization.isEmpty())
        args << options.optimization;
    if (options.debugInfo)
        args << QStringLiteral("-g");
    for (const QString &define : options.defines)
        args << QStringLiteral("-D") + define;
    for (const QString &dir : options.includeDirs)
        args << QStringLiteral("-I") + dir;
    args << options.extraArgs;
    args << diagnosticFlags;
    return args;
}

QStringList linkFlags(const BuildOptions &options)
{
    QStringList args;
    for (const QString &dir : options.libraryDirs)
        args << QStringLiteral("-L") + dir;
    for (const QString &library : options.libraries)
        args << QStringLiteral("-l") + library;
    return args;
}

QString driverFor(const Toolchain &tools, SourceLanguage language)
{
    return language == SourceLanguage::Cpp ? tools.gxx : tools.gcc;
}

} // namespace

QString CommandLine::display() const
{
    QStringList parts{shellQuote(program)};
    for (const QString &argument : arguments)
        parts << shellQuote(argument);
    return parts.join(QLatin1Char(' '));
}

namespace CompileCommand {

SourceLanguage languageOf(const QString &sourcePath)
{
    // Case-sensitive on purpose: GCC treats .C as C++.
    return QFileInfo(sourcePath).suffix() == QLatin1String("c") ? SourceLanguage::C
                                                                : SourceLanguage::Cpp;
}

CommandLine singleFile(const Toolchain &tools, const BuildOptions &options,
                       const QString &sourcePath, const QString &outputPath)
{
    const SourceLanguage language = languageOf(sourcePath);
    CommandLine command{driverFor(tools, language), compileFlags(options, language)};
    command.arguments << sourcePath << QStringLiteral("-o") << outputPath;
    command.arguments << linkFlags(options);
    return command;
}

CommandLine object(const Toolchain &tools, const BuildOptions &options,
                   const QString &sourcePath, const QString &objectPath)
{
    const SourceLanguage language = languageOf(sourcePath);
    CommandLine command{driverFor(tools, language), compileFlags(options, language)};
    command.arguments << QStringLiteral("-MMD") << QStringLiteral("-MP") << QStringLiteral("-c")
                      << sourcePath << QStringLiteral("-o") << objectPath;
    return command;
}

CommandLine link(const Toolchain &tools, const BuildOptions &options, bool hasCpp,
                 const QStringList &objects, const QString &outputPath, bool shared)
{
    CommandLine command{hasCpp ? tools.gxx : tools.gcc, {}};
    if (shared)
        command.arguments << QStringLiteral("-shared");
    command.arguments << objects << QStringLiteral("-o") << outputPath;
    command.arguments << linkFlags(options);
    return command;
}

CommandLine archive(const QStringList &objects, const QString &outputPath)
{
    CommandLine command{QStringLiteral("ar"), {QStringLiteral("rcs"), outputPath}};
    command.arguments << objects;
    return command;
}

QString outputPathFor(const QString &sourcePath, const QString &outputDirectory,
                      const QString &customName)
{
    const QFileInfo source(sourcePath);
    const QString name = customName.isEmpty() ? source.completeBaseName() : customName;
    QDir directory = source.absoluteDir();
    if (!outputDirectory.isEmpty())
        directory = QDir(directory.absoluteFilePath(outputDirectory));
    return QDir::cleanPath(directory.absoluteFilePath(name));
}

QStringList splitArguments(const QString &text)
{
    return QProcess::splitCommand(text);
}

} // namespace CompileCommand
