#include "leetcode/ProblemFolder.h"

#include "filesystem/FileManager.h"
#include "leetcode/CppSignature.h"
#include "leetcode/QuestionParser.h"
#include "leetcode/SolutionFile.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include <algorithm>

static void initLeetCodeResources()
{
    Q_INIT_RESOURCE(leetcode);
}

namespace {

constexpr auto generatorVersion = "0.1.0";

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void writeText(const QString &path, const QString &text)
{
    QString error;
    if (!FileManager::save(path, text, &error))
        throw LcError(QStringLiteral("Could not write %1: %2").arg(path, error));
}

void ensureDirectory(const QString &directory)
{
    if (!QDir().mkpath(directory))
        throw LcError(QStringLiteral("Could not create the folder %1.").arg(directory));
}

QJsonArray paramsToJson(const QList<MetaParam> &params)
{
    QJsonArray array;
    for (const MetaParam &param : params)
        array.append(QJsonObject{{QStringLiteral("name"), param.name}, {QStringLiteral("type"), param.type}});
    return array;
}

QList<MetaParam> paramsFromJson(const QJsonValue &value)
{
    QList<MetaParam> params;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        const QJsonObject param = entry.toObject();
        params.append({param.value(QLatin1String("name")).toString(),
                       param.value(QLatin1String("type")).toString()});
    }
    return params;
}

QJsonObject metaToJson(const ProblemMeta &meta)
{
    QJsonObject object{{QStringLiteral("name"), meta.name},
                       {QStringLiteral("params"), paramsToJson(meta.params)},
                       {QStringLiteral("returnType"), meta.returnType},
                       {QStringLiteral("systemDesign"), meta.systemDesign},
                       {QStringLiteral("manual"), meta.manual}};
    if (meta.systemDesign) {
        object.insert(QStringLiteral("constructorParams"), paramsToJson(meta.constructorParams));
        QJsonArray methods;
        for (const MetaMethod &method : meta.methods) {
            methods.append(QJsonObject{{QStringLiteral("name"), method.name},
                                       {QStringLiteral("params"), paramsToJson(method.params)},
                                       {QStringLiteral("returnType"), method.returnType}});
        }
        object.insert(QStringLiteral("methods"), methods);
    }
    if (meta.hasOutput) {
        QJsonObject output{{QStringLiteral("paramIndex"), meta.outputParamIndex}};
        if (!meta.outputSize.isEmpty())
            output.insert(QStringLiteral("size"), meta.outputSize);
        object.insert(QStringLiteral("output"), output);
    }
    return object;
}

ProblemMeta metaFromJson(const QJsonObject &object)
{
    ProblemMeta meta;
    meta.name = object.value(QLatin1String("name")).toString();
    meta.params = paramsFromJson(object.value(QLatin1String("params")));
    meta.returnType = object.value(QLatin1String("returnType")).toString(QStringLiteral("void"));
    meta.systemDesign = object.value(QLatin1String("systemDesign")).toBool();
    meta.manual = object.value(QLatin1String("manual")).toBool();
    meta.constructorParams = paramsFromJson(object.value(QLatin1String("constructorParams")));

    const QJsonArray methods = object.value(QLatin1String("methods")).toArray();
    for (const QJsonValue &entry : methods) {
        const QJsonObject method = entry.toObject();
        meta.methods.append({method.value(QLatin1String("name")).toString(),
                             paramsFromJson(method.value(QLatin1String("params"))),
                             method.value(QLatin1String("returnType")).toString(QStringLiteral("void"))});
    }

    const QJsonObject output = object.value(QLatin1String("output")).toObject();
    if (output.contains(QLatin1String("paramIndex"))) {
        meta.hasOutput = true;
        meta.outputParamIndex = output.value(QLatin1String("paramIndex")).toInt();
        meta.outputSize = output.value(QLatin1String("size")).toVariant().toString();
    }
    return meta;
}

LcTestCase parseTestEntry(const LcValue &item, qsizetype index, const QStringList &keys)
{
    const QString where = QStringLiteral("tests.json, test #%1").arg(index + 1);
    if (!item.isObject())
        throw LcError(where + QStringLiteral(": each test must be an object with an \"input\" field."));

    const LcValue *input = item.entry(QStringLiteral("input"));
    if (!input || !input->isObject()) {
        QStringList shape;
        for (const QString &key : keys)
            shape << QStringLiteral("\"%1\": ...").arg(key);
        throw LcError(where + QStringLiteral(": \"input\" must be an object like { %1 }.")
                                  .arg(shape.join(QStringLiteral(", "))));
    }

    LcTestCase test;
    test.id = static_cast<int>(index) + 1;
    for (const QString &key : keys) {
        const LcValue *value = input->entry(key);
        if (!value)
            throw LcError(where + QStringLiteral(": missing input \"%1\".").arg(key));
        test.inputs.append(*value);
    }
    for (const auto &entry : input->entries) {
        if (!keys.contains(entry.first)) {
            throw LcError(where + QStringLiteral(": unknown input \"%1\" (expected %2).")
                                      .arg(entry.first, keys.join(QStringLiteral(", "))));
        }
    }

    const LcValue *name = item.entry(QStringLiteral("name"));
    test.name = name && name->kind == LcValue::Kind::String && !name->text.isEmpty()
                    ? name->text
                    : QStringLiteral("Test %1").arg(index + 1);
    const LcValue *source = item.entry(QStringLiteral("source"));
    test.source = source && source->kind == LcValue::Kind::String && source->text == u"example"
                      ? TestSource::Example
                      : TestSource::Custom;
    if (const LcValue *expected = item.entry(QStringLiteral("expected")))
        test.expected = *expected;
    return test;
}

} // namespace

namespace LeetCodeBuilder {

QString runtimeSource()
{
    initLeetCodeResources();
    return readText(QStringLiteral(":/leetcode/lc_runtime.hpp"));
}

QString signatureHash(const QString &starterCode)
{
    const CppStarter starter = CppSignature::analyzeStarterCode(starterCode);
    QStringList classes;
    for (const CppClass &cls : starter.classes) {
        QStringList methods;
        for (const CppMethod &method : cls.methods) {
            QStringList params;
            for (const CppParam &param : method.params)
                params << CppSignature::formatType(param.type);
            methods << QStringLiteral("%1 %2(%3)")
                           .arg(method.hasReturnType ? CppSignature::formatType(method.returnType) : QString(),
                                method.name, params.join(QLatin1Char(',')));
        }
        classes << QStringLiteral("%1{%2}").arg(cls.name, methods.join(QLatin1Char(';')));
    }
    const QByteArray hash = QCryptographicHash::hash(classes.join(QLatin1Char('|')).toUtf8(),
                                                     QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex().left(16));
}

ProblemMetadata buildMetadata(const LeetCodeProblem &problem)
{
    const HarnessPlan plan =
        HarnessGenerator::planHarness(problem.meta, CppSignature::analyzeStarterCode(problem.starterCode));

    ProblemMetadata metadata;
    metadata.id = problem.frontendId.toInt();
    metadata.questionId = problem.id;
    metadata.slug = problem.slug;
    metadata.title = problem.title;
    metadata.difficulty = problem.difficulty;
    metadata.url = problem.url;
    switch (plan.kind) {
    case HarnessPlan::Kind::Function: metadata.support = QStringLiteral("function"); break;
    case HarnessPlan::Kind::Design: metadata.support = QStringLiteral("design"); break;
    case HarnessPlan::Kind::Manual:
        metadata.support = QStringLiteral("manual");
        metadata.supportReason = plan.reason;
        metadata.manualHarness = true;
        break;
    }
    metadata.comparison = OutputComparator::detectMode(problem.slug, problem.meta, problem.contentHtml);
    metadata.meta = problem.meta;
    metadata.starterCode = problem.starterCode;
    metadata.signatureHash = signatureHash(problem.starterCode);
    metadata.fetchedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    metadata.generatorVersion = QLatin1String(generatorVersion);
    return metadata;
}

QString buildHeader(const ProblemMetadata &metadata)
{
    const CppStarter starter = CppSignature::analyzeStarterCode(metadata.starterCode);
    return SolutionFile::generateHeader(runtimeSource(), SolutionFile::requiredDefinitions(starter));
}

static GeneratedHarness regenerateHarness(const ProblemMetadata &metadata, const QList<LcTestCase> &tests)
{
    const CppStarter starter = CppSignature::analyzeStarterCode(metadata.starterCode);
    QStringList knownTypes;
    const QList<CommentedDefinition> definitions = SolutionFile::requiredDefinitions(starter);
    for (const CommentedDefinition &definition : definitions)
        knownTypes << definition.name;
    return HarnessGenerator::generate(HarnessGenerator::planHarness(metadata.meta, starter),
                                      metadata.meta, tests, knownTypes);
}

QString syncHarness(const QString &source, const ProblemMetadata &metadata,
                    const QList<LcTestCase> &tests, QStringList *warnings)
{
    if (metadata.manualHarness)
        return source;
    const GeneratedHarness harness = regenerateHarness(metadata, tests);
    if (warnings)
        warnings->append(harness.warnings);
    return SolutionFile::replaceHarness(source, harness.code);
}

QList<LcTestCase> mergeExamples(const QList<LcTestCase> &existing, const QList<LcTestCase> &examples)
{
    QList<LcTestCase> merged = examples;
    for (const LcTestCase &test : existing) {
        if (test.source == TestSource::Custom)
            merged.append(test);
    }
    for (qsizetype i = 0; i < merged.size(); ++i)
        merged[i].id = static_cast<int>(i) + 1;
    return merged;
}

QList<LcTestCase> parseTests(const QString &text, const QStringList &keys)
{
    LcValue root;
    const QString trimmed = text.trimmed();
    try {
        root = LcValueParser::parse(trimmed.isEmpty() ? QStringLiteral("[]") : trimmed);
    } catch (const LcError &error) {
        throw LcError(QStringLiteral("tests.json is not valid JSON: %1").arg(error.message()));
    }
    if (!root.isArray())
        throw LcError(QStringLiteral("tests.json must contain an array of tests."));

    QList<LcTestCase> tests;
    for (qsizetype i = 0; i < root.items.size(); ++i)
        tests.append(parseTestEntry(root.items.at(i), i, keys));
    return tests;
}

QString serializeTests(const QList<LcTestCase> &tests, const QStringList &keys)
{
    if (tests.isEmpty())
        return QStringLiteral("[]\n");

    QStringList entries;
    for (const LcTestCase &test : tests) {
        QStringList lines = {
            QStringLiteral("        \"name\": ") + jsonString(test.name),
            QStringLiteral("        \"source\": ")
                + jsonString(test.source == TestSource::Example ? QStringLiteral("example") : QStringLiteral("custom")),
        };

        QStringList inputs;
        for (qsizetype i = 0; i < keys.size(); ++i) {
            const LcValue value = i < test.inputs.size() ? test.inputs.at(i) : LcValue();
            inputs << QStringLiteral("            %1: %2").arg(jsonString(keys.at(i)), toJsonText(value, 12));
        }
        lines << QStringLiteral("        \"input\": {\n%1\n        }").arg(inputs.join(QStringLiteral(",\n")));
        if (test.expected)
            lines << QStringLiteral("        \"expected\": ") + toJsonText(*test.expected, 8);
        entries << QStringLiteral("    {\n%1\n    }").arg(lines.join(QStringLiteral(",\n")));
    }
    return QStringLiteral("[\n%1\n]\n").arg(entries.join(QStringLiteral(",\n")));
}

} // namespace LeetCodeBuilder

ProblemFolder::ProblemFolder(const QString &directory)
    : m_directory(directory)
{
}

QString ProblemFolder::solutionPath() const
{
    return QDir(m_directory).filePath(QStringLiteral("solution.cpp"));
}

QString ProblemFolder::metadataPath() const
{
    return QDir(m_directory).filePath(QStringLiteral("metadata.json"));
}

QString ProblemFolder::testsPath() const
{
    return QDir(m_directory).filePath(QStringLiteral("tests.json"));
}

QString ProblemFolder::supportDirectory() const
{
    return QDir(m_directory).filePath(QStringLiteral(".lc"));
}

QString ProblemFolder::headerPath() const
{
    return QDir(supportDirectory()).filePath(QStringLiteral("leetcode.hpp"));
}

QString ProblemFolder::descriptionPath() const
{
    return QDir(supportDirectory()).filePath(QStringLiteral("description.html"));
}

QString ProblemFolder::binaryPath(bool debug) const
{
    return QDir(supportDirectory()).filePath(debug ? QStringLiteral("solution-debug") : QStringLiteral("solution"));
}

bool ProblemFolder::isProblem() const
{
    return !m_directory.isEmpty() && QFileInfo::exists(metadataPath()) && QFileInfo::exists(solutionPath());
}

QString ProblemFolder::folderOfSolution(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (info.fileName() != u"solution.cpp")
        return {};
    const QString directory = info.absolutePath();
    return ProblemFolder(directory).isProblem() ? directory : QString();
}

ProblemMetadata ProblemFolder::readMetadata() const
{
    QFile file(metadataPath());
    if (!file.open(QIODevice::ReadOnly)) {
        throw LcError(QStringLiteral("No metadata.json in %1. Is this a LeetCode problem folder? Use "
                                     "LeetCode > Fetch Problem to create one.").arg(m_directory));
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw LcError(QStringLiteral("metadata.json is not valid JSON."));

    const QJsonObject data = document.object();
    const auto text = [&data](const char *key) { return data.value(QLatin1String(key)).toString(); };
    if (text("slug").isEmpty() || !data.value(QLatin1String("meta")).isObject()
        || !data.value(QLatin1String("starterCode")).isString()) {
        throw LcError(QStringLiteral("metadata.json is missing required fields (slug, meta, "
                                     "starterCode). Use LeetCode > Refresh Problem to rebuild it."));
    }

    ProblemMetadata metadata;
    metadata.id = data.value(QLatin1String("id")).toInt();
    metadata.questionId = text("questionId");
    metadata.slug = text("slug");
    metadata.title = text("title").isEmpty() ? metadata.slug : text("title");
    metadata.difficulty = text("difficulty").isEmpty() ? QStringLiteral("Unknown") : text("difficulty");
    metadata.url = text("url").isEmpty() ? QuestionParser::problemUrl(metadata.slug) : text("url");
    if (!text("support").isEmpty())
        metadata.support = text("support");
    metadata.supportReason = text("supportReason");
    metadata.manualHarness = text("harness") == u"manual";
    if (!text("comparison").isEmpty()) {
        const auto mode = OutputComparator::modeFromName(text("comparison"));
        if (!mode) {
            throw LcError(QStringLiteral("metadata.json: \"comparison\" must be one of exact, "
                                         "unordered, unordered-deep."));
        }
        metadata.comparison = *mode;
    }
    metadata.meta = metaFromJson(data.value(QLatin1String("meta")).toObject());
    metadata.starterCode = text("starterCode");
    metadata.signatureHash = text("signatureHash");
    metadata.fetchedAt = text("fetchedAt");
    if (!text("generator").isEmpty())
        metadata.generator = text("generator");
    metadata.generatorVersion = text("generatorVersion");
    return metadata;
}

void ProblemFolder::writeMetadata(const ProblemMetadata &metadata) const
{
    QJsonObject object{{QStringLiteral("id"), metadata.id},
                       {QStringLiteral("questionId"), metadata.questionId},
                       {QStringLiteral("slug"), metadata.slug},
                       {QStringLiteral("title"), metadata.title},
                       {QStringLiteral("difficulty"), metadata.difficulty},
                       {QStringLiteral("language"), QStringLiteral("cpp")},
                       {QStringLiteral("url"), metadata.url},
                       {QStringLiteral("support"), metadata.support},
                       {QStringLiteral("harness"), metadata.manualHarness ? QStringLiteral("manual")
                                                                          : QStringLiteral("generated")},
                       {QStringLiteral("comparison"), OutputComparator::modeName(metadata.comparison)},
                       {QStringLiteral("meta"), metaToJson(metadata.meta)},
                       {QStringLiteral("starterCode"), metadata.starterCode},
                       {QStringLiteral("signatureHash"), metadata.signatureHash},
                       {QStringLiteral("fetchedAt"), metadata.fetchedAt},
                       {QStringLiteral("generator"), metadata.generator},
                       {QStringLiteral("generatorVersion"), metadata.generatorVersion}};
    if (!metadata.supportReason.isEmpty())
        object.insert(QStringLiteral("supportReason"), metadata.supportReason);
    writeText(metadataPath(), QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));
}

QList<LcTestCase> ProblemFolder::readTests(const ProblemMetadata &metadata) const
{
    if (!QFileInfo::exists(testsPath()))
        return {};
    return LeetCodeBuilder::parseTests(readText(testsPath()), QuestionParser::inputKeys(metadata.meta));
}

void ProblemFolder::writeTests(const ProblemMetadata &metadata, const QList<LcTestCase> &tests) const
{
    writeText(testsPath(), LeetCodeBuilder::serializeTests(tests, QuestionParser::inputKeys(metadata.meta)));
}

QStringList ProblemFolder::create(const LeetCodeProblem &problem, bool overwrite) const
{
    ensureDirectory(supportDirectory());

    ProblemMetadata metadata = LeetCodeBuilder::buildMetadata(problem);
    QList<LcTestCase> tests = problem.examples;
    QStringList warnings = problem.warnings;

    const bool exists = QFileInfo::exists(solutionPath());
    if (exists && !overwrite) {
        // Keep the user's code and refresh everything else.
        try {
            const ProblemMetadata existing = readMetadata();
            tests = LeetCodeBuilder::mergeExamples(readTests(existing), tests);
            metadata.comparison = existing.comparison;
            metadata.manualHarness = existing.manualHarness && metadata.manualHarness;
        } catch (const LcError &error) {
            warnings << QStringLiteral("Existing tests were not kept: %1").arg(error.message());
        }
    } else {
        if (exists) {
            const QString backup = QDir(supportDirectory()).filePath(QStringLiteral("solution.cpp.bak"));
            QFile::remove(backup);
            QFile::copy(solutionPath(), backup);
        }
        const CppStarter starter = CppSignature::analyzeStarterCode(problem.starterCode);
        QStringList knownTypes;
        const QList<CommentedDefinition> definitions = SolutionFile::requiredDefinitions(starter);
        for (const CommentedDefinition &definition : definitions)
            knownTypes << definition.name;
        const GeneratedHarness harness = HarnessGenerator::generate(
            HarnessGenerator::planHarness(problem.meta, starter), problem.meta, tests, knownTypes);
        warnings << harness.warnings;
        writeText(solutionPath(), SolutionFile::generate({problem.frontendId, problem.title, problem.difficulty,
                                                          problem.url, problem.starterCode, harness.code}));
    }

    writeMetadata(metadata);
    writeTests(metadata, tests);
    writeText(headerPath(), LeetCodeBuilder::buildHeader(metadata));
    if (!problem.contentHtml.isEmpty())
        writeText(descriptionPath(), problem.contentHtml);
    return warnings;
}

bool ProblemFolder::refresh(const LeetCodeProblem &problem) const
{
    const ProblemMetadata current = readMetadata();
    ProblemMetadata fresh = LeetCodeBuilder::buildMetadata(problem);
    fresh.comparison = current.comparison;
    fresh.manualHarness = current.manualHarness || fresh.manualHarness;

    QList<LcTestCase> existing;
    try {
        existing = readTests(current);
    } catch (const LcError &) {
        // An unreadable tests.json is rebuilt from the examples.
    }

    ensureDirectory(supportDirectory());
    writeMetadata(fresh);
    writeTests(fresh, LeetCodeBuilder::mergeExamples(existing, problem.examples));
    writeText(headerPath(), LeetCodeBuilder::buildHeader(fresh));
    if (!problem.contentHtml.isEmpty())
        writeText(descriptionPath(), problem.contentHtml);
    return current.signatureHash != fresh.signatureHash;
}

QStringList ProblemFolder::compileArguments(const LeetCodeRunSettings &settings, bool debug) const
{
    QStringList flags = settings.flags;
    if (debug) {
        flags.erase(std::remove_if(flags.begin(), flags.end(),
                                   [](const QString &flag) { return flag.startsWith(QLatin1String("-O")); }),
                    flags.end());
        flags << QStringLiteral("-g") << QStringLiteral("-O0");
    }
    if (settings.sanitizers) {
        flags << QStringLiteral("-fsanitize=address,undefined") << QStringLiteral("-fno-omit-frame-pointer")
              << QStringLiteral("-g");
    }

    QStringList arguments{QStringLiteral("-std=") + settings.standard};
    arguments << flags << QStringLiteral("-fdiagnostics-color=never")
              << QStringLiteral("-fdiagnostics-column-unit=byte") << QStringLiteral("-fmessage-length=0")
              << solutionPath() << QStringLiteral("-o") << binaryPath(debug);
    return arguments;
}

QString ProblemFolder::buildHash(const LeetCodeRunSettings &settings, bool debug) const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(settings.compiler.toUtf8());
    hash.addData(compileArguments(settings, debug).join(QChar()).toUtf8());
    for (const QString &path : {solutionPath(), headerPath()}) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        hash.addData(file.readAll());
    }
    return QString::fromLatin1(hash.result().toHex());
}

BuildPlan ProblemFolder::makeBuildPlan(const ProblemMetadata &metadata, const LeetCodeRunSettings &settings,
                                       bool debug) const
{
    ensureDirectory(supportDirectory());

    const QString header = LeetCodeBuilder::buildHeader(metadata);
    if (readText(headerPath()) != header)
        writeText(headerPath(), header);

    BuildPlan plan;
    plan.workingDirectory = m_directory;
    plan.outputPath = binaryPath(debug);

    const QString hash = buildHash(settings, debug);
    const QString stored = readText(binaryPath(debug) + QStringLiteral(".hash"));
    if (hash.isEmpty() || hash != stored || !QFileInfo::exists(binaryPath(debug))) {
        const CommandLine command{settings.compiler, compileArguments(settings, debug)};
        plan.compileJobs.append(BuildJob{command, QStringLiteral("solution.cpp")});
    }
    return plan;
}

void ProblemFolder::markBuilt(const LeetCodeRunSettings &settings, bool debug) const
{
    QFile file(binaryPath(debug) + QStringLiteral(".hash"));
    if (file.open(QIODevice::WriteOnly))
        file.write(buildHash(settings, debug).toLatin1());
}
