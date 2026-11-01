#include "project/Project.h"

#include "filesystem/FileManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace {

QJsonArray toJson(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

QStringList toStringList(const QJsonValue &value)
{
    QStringList list;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array)
        list << entry.toString();
    return list;
}

QString targetTypeName(TargetType type)
{
    switch (type) {
    case TargetType::StaticLibrary: return QStringLiteral("static-library");
    case TargetType::SharedLibrary: return QStringLiteral("shared-library");
    case TargetType::Executable: break;
    }
    return QStringLiteral("executable");
}

TargetType targetTypeFrom(const QString &name)
{
    if (name == QLatin1String("static-library"))
        return TargetType::StaticLibrary;
    if (name == QLatin1String("shared-library"))
        return TargetType::SharedLibrary;
    return TargetType::Executable;
}

QString configurationName(BuildConfiguration configuration)
{
    return configuration == BuildConfiguration::Debug ? QStringLiteral("debug")
                                                      : QStringLiteral("release");
}

QString objectPathFor(const QString &objectRoot, const QString &relativeSource)
{
    QString mapped = relativeSource;
    mapped.replace(QLatin1String(".."), QLatin1String("__"));
    return QDir(objectRoot).filePath(mapped + QStringLiteral(".o"));
}

bool objectIsCurrent(const QString &source, const QString &object)
{
    const QFileInfo objectInfo(object);
    if (!objectInfo.exists())
        return false;
    const QDateTime builtAt = objectInfo.lastModified();
    if (QFileInfo(source).lastModified() > builtAt)
        return false;

    QString dependencyPath = object;
    dependencyPath.chop(1);
    dependencyPath += QLatin1Char('d');
    QFile dependencies(dependencyPath);
    if (!dependencies.open(QIODevice::ReadOnly))
        return false;

    const QStringList prerequisites = Project::parseDependencyFile(dependencies.readAll());
    return std::all_of(prerequisites.cbegin(), prerequisites.cend(),
                       [&builtAt](const QString &prerequisite) {
                           const QFileInfo info(prerequisite);
                           return info.exists() && info.lastModified() <= builtAt;
                       });
}

} // namespace

QString Project::directory() const
{
    return QFileInfo(m_filePath).absolutePath();
}

bool Project::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QCoreApplication::translate("Project", "Invalid project file: %1")
                     .arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    const Project defaults;
    m_filePath = QFileInfo(path).absoluteFilePath();
    name = root.value(QLatin1String("name")).toString(QFileInfo(path).completeBaseName());
    targetName = root.value(QLatin1String("target")).toString(name);
    targetType = targetTypeFrom(root.value(QLatin1String("targetType")).toString());
    sources = toStringList(root.value(QLatin1String("sources")));
    headers = toStringList(root.value(QLatin1String("headers")));
    folders = toStringList(root.value(QLatin1String("folders")));
    runArguments = root.value(QLatin1String("runArguments")).toString();
    workingDirectory = root.value(QLatin1String("workingDirectory")).toString();

    const QJsonObject compiler = root.value(QLatin1String("compiler")).toObject();
    cStandard = compiler.value(QLatin1String("cStandard")).toString(defaults.cStandard);
    cxxStandard = compiler.value(QLatin1String("cxxStandard")).toString(defaults.cxxStandard);
    debugOptimization =
        compiler.value(QLatin1String("debugOptimization")).toString(defaults.debugOptimization);
    releaseOptimization = compiler.value(QLatin1String("releaseOptimization"))
                              .toString(defaults.releaseOptimization);
    warningFlags = compiler.contains(QLatin1String("warningFlags"))
                       ? toStringList(compiler.value(QLatin1String("warningFlags")))
                       : defaults.warningFlags;
    defines = toStringList(compiler.value(QLatin1String("defines")));
    includeDirs = toStringList(compiler.value(QLatin1String("includeDirs")));
    libraryDirs = toStringList(compiler.value(QLatin1String("libraryDirs")));
    libraries = toStringList(compiler.value(QLatin1String("libraries")));
    extraArgs = compiler.value(QLatin1String("extraArgs")).toString();
    return true;
}

bool Project::save(QString *error) const
{
    QJsonObject compiler;
    compiler.insert(QLatin1String("cStandard"), cStandard);
    compiler.insert(QLatin1String("cxxStandard"), cxxStandard);
    compiler.insert(QLatin1String("debugOptimization"), debugOptimization);
    compiler.insert(QLatin1String("releaseOptimization"), releaseOptimization);
    compiler.insert(QLatin1String("warningFlags"), toJson(warningFlags));
    compiler.insert(QLatin1String("defines"), toJson(defines));
    compiler.insert(QLatin1String("includeDirs"), toJson(includeDirs));
    compiler.insert(QLatin1String("libraryDirs"), toJson(libraryDirs));
    compiler.insert(QLatin1String("libraries"), toJson(libraries));
    compiler.insert(QLatin1String("extraArgs"), extraArgs);

    QJsonObject root;
    root.insert(QLatin1String("name"), name);
    root.insert(QLatin1String("target"), targetName);
    root.insert(QLatin1String("targetType"), targetTypeName(targetType));
    root.insert(QLatin1String("sources"), toJson(sources));
    root.insert(QLatin1String("headers"), toJson(headers));
    root.insert(QLatin1String("folders"), toJson(folders));
    root.insert(QLatin1String("runArguments"), runArguments);
    root.insert(QLatin1String("workingDirectory"), workingDirectory);
    root.insert(QLatin1String("compiler"), compiler);

    QSaveFile file(m_filePath);
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

QString Project::absolutePath(const QString &relativePath) const
{
    return QDir::cleanPath(QDir(directory()).absoluteFilePath(relativePath));
}

QString Project::relativePath(const QString &absolutePath) const
{
    return QDir(directory()).relativeFilePath(absolutePath);
}

bool Project::contains(const QString &absolutePath) const
{
    const QString relative = relativePath(absolutePath);
    return sources.contains(relative) || headers.contains(relative);
}

bool Project::addFile(const QString &absolutePath)
{
    if (contains(absolutePath))
        return false;
    const QString relative = relativePath(absolutePath);
    if (FileManager::isSourceFile(absolutePath))
        sources.append(relative);
    else if (FileManager::isHeaderFile(absolutePath))
        headers.append(relative);
    else
        return false;
    return true;
}

bool Project::removeFile(const QString &absolutePath)
{
    const QString relative = relativePath(absolutePath);
    return sources.removeAll(relative) + headers.removeAll(relative) > 0;
}

BuildOptions Project::buildOptions(BuildConfiguration configuration) const
{
    BuildOptions options;
    options.cStandard = cStandard;
    options.cxxStandard = cxxStandard;
    options.warningFlags = warningFlags;
    options.defines = defines;
    options.libraries = libraries;
    options.extraArgs = CompileCommand::splitArguments(extraArgs);
    for (const QString &dir : includeDirs)
        options.includeDirs << absolutePath(dir);
    for (const QString &dir : libraryDirs)
        options.libraryDirs << absolutePath(dir);

    if (configuration == BuildConfiguration::Debug) {
        options.optimization = debugOptimization;
        options.debugInfo = true;
    } else {
        options.optimization = releaseOptimization;
        options.debugInfo = false;
        options.defines << QStringLiteral("NDEBUG");
    }
    if (targetType == TargetType::SharedLibrary)
        options.extraArgs << QStringLiteral("-fPIC");
    return options;
}

QString Project::buildDirectory(BuildConfiguration configuration) const
{
    return absolutePath(QStringLiteral("build/") + configurationName(configuration));
}

QString Project::outputPath(BuildConfiguration configuration) const
{
    QString file;
    switch (targetType) {
    case TargetType::Executable: file = targetName; break;
    case TargetType::StaticLibrary: file = QStringLiteral("lib%1.a").arg(targetName); break;
    case TargetType::SharedLibrary: file = QStringLiteral("lib%1.so").arg(targetName); break;
    }
    return QDir(buildDirectory(configuration)).filePath(file);
}

QString Project::runWorkingDirectory() const
{
    return workingDirectory.isEmpty() ? directory() : absolutePath(workingDirectory);
}

BuildPlan Project::makePlan(const Toolchain &tools, BuildConfiguration configuration) const
{
    const BuildOptions options = buildOptions(configuration);
    const QString buildDir = buildDirectory(configuration);
    const QString objectRoot = QDir(buildDir).filePath(QStringLiteral("obj"));

    BuildPlan plan;
    plan.workingDirectory = directory();
    plan.outputPath = outputPath(configuration);
    plan.directories << buildDir << objectRoot;

    // Any change to the command line invalidates every object.
    QStringList fingerprintParts;
    bool hasCpp = false;
    QStringList objects;

    struct Unit
    {
        QString source;
        QString object;
    };
    QList<Unit> units;
    for (const QString &relative : sources) {
        const QString source = absolutePath(relative);
        const QString object = objectPathFor(objectRoot, relative);
        units.append(Unit{source, object});
        objects << object;
        hasCpp = hasCpp || CompileCommand::languageOf(source) == SourceLanguage::Cpp;
        plan.directories << QFileInfo(object).absolutePath();
        fingerprintParts << CompileCommand::object(tools, options, source, object).display();
    }

    QDir().mkpath(buildDir);
    const QString stampPath = QDir(buildDir).filePath(QStringLiteral("flags.stamp"));
    const QByteArray fingerprint = fingerprintParts.join(QLatin1Char('\n')).toUtf8();
    QFile stamp(stampPath);
    const bool flagsChanged = !stamp.open(QIODevice::ReadOnly) || stamp.readAll() != fingerprint;
    stamp.close();
    if (flagsChanged && stamp.open(QIODevice::WriteOnly))
        stamp.write(fingerprint);

    for (const Unit &unit : units) {
        if (flagsChanged || !objectIsCurrent(unit.source, unit.object)) {
            plan.compileJobs.append(
                BuildJob{CompileCommand::object(tools, options, unit.source, unit.object),
                         QFileInfo(unit.source).fileName()});
        }
    }

    const QFileInfo output(plan.outputPath);
    bool needsLink = !plan.compileJobs.isEmpty() || !output.exists();
    if (!needsLink) {
        needsLink = std::any_of(objects.cbegin(), objects.cend(), [&output](const QString &object) {
            return QFileInfo(object).lastModified() > output.lastModified();
        });
    }
    if (needsLink && !objects.isEmpty()) {
        const CommandLine command =
            targetType == TargetType::StaticLibrary
                ? CompileCommand::archive(objects, plan.outputPath)
                : CompileCommand::link(tools, options, hasCpp, objects, plan.outputPath,
                                       targetType == TargetType::SharedLibrary);
        plan.linkJob = BuildJob{command, QFileInfo(plan.outputPath).fileName()};
    }
    return plan;
}

Project Project::create(const QString &directory, const QString &name, bool cpp)
{
    Project project;
    project.name = name;
    project.targetName = name;
    project.setFilePath(QDir(directory).filePath(name + QLatin1Char('.')
                                                 + QLatin1String(extension)));
    project.sources << (cpp ? QStringLiteral("main.cpp") : QStringLiteral("main.c"));
    return project;
}

QStringList Project::parseDependencyFile(const QByteArray &contents)
{
    // Paths are space separated, with spaces escaped as "\ " and lines continued
    // with a trailing backslash. Only the first rule matters: -MP appends empty
    // phony rules for the headers.
    const QString text = QString::fromUtf8(contents);
    QStringList prerequisites;
    QString current;
    bool inPrerequisites = false;

    const auto flush = [&] {
        if (!current.isEmpty())
            prerequisites << current;
        current.clear();
    };

    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!inPrerequisites) {
            if (c == QLatin1Char('\\') && i + 1 < text.size()) {
                ++i;
            } else if (c == QLatin1Char(':')
                       && (i + 1 >= text.size() || text.at(i + 1).isSpace())) {
                inPrerequisites = true;
            }
            continue;
        }

        if (c == QLatin1Char('\\') && i + 1 < text.size()) {
            const QChar next = text.at(i + 1);
            if (next == QLatin1Char('\n')) {
                ++i;
                flush();
            } else if (next == QLatin1Char(' ') || next == QLatin1Char('\\')
                       || next == QLatin1Char('#')) {
                current += next;
                ++i;
            } else {
                current += c;
            }
        } else if (c == QLatin1Char('$') && i + 1 < text.size()
                   && text.at(i + 1) == QLatin1Char('$')) {
            current += c;
            ++i;
        } else if (c == QLatin1Char('\n')) {
            flush();
            break;
        } else if (c.isSpace()) {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return prerequisites;
}
