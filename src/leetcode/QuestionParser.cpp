#include "leetcode/QuestionParser.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace {

struct ExampleBlock
{
    std::optional<QString> input;
    std::optional<QString> output;
};

const QRegularExpression &slugPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^[a-z0-9]+(?:-[a-z0-9]+)*$)"));
    return pattern;
}

QString decodeEntities(const QString &text)
{
    static const QHash<QString, QString> named = {
        {QStringLiteral("amp"), QStringLiteral("&")},
        {QStringLiteral("lt"), QStringLiteral("<")},
        {QStringLiteral("gt"), QStringLiteral(">")},
        {QStringLiteral("quot"), QStringLiteral("\"")},
        {QStringLiteral("apos"), QStringLiteral("'")},
        {QStringLiteral("nbsp"), QStringLiteral(" ")},
        {QStringLiteral("le"), QString::fromUtf8("≤")},
        {QStringLiteral("ge"), QString::fromUtf8("≥")},
        {QStringLiteral("ne"), QString::fromUtf8("≠")},
        {QStringLiteral("times"), QString::fromUtf8("×")},
        {QStringLiteral("minus"), QString::fromUtf8("−")},
        {QStringLiteral("hellip"), QString::fromUtf8("…")},
        {QStringLiteral("rarr"), QString::fromUtf8("→")},
        {QStringLiteral("larr"), QString::fromUtf8("←")},
        {QStringLiteral("ndash"), QString::fromUtf8("–")},
        {QStringLiteral("mdash"), QString::fromUtf8("—")},
    };
    static const QRegularExpression entity(QStringLiteral("&(#[xX][0-9a-fA-F]+|#\\d+|[a-zA-Z]+);"));

    QString out;
    qsizetype copied = 0;
    auto it = entity.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        out += QStringView(text).sliced(copied, match.capturedStart() - copied);
        copied = match.capturedEnd();

        const QString body = match.captured(1);
        if (body.startsWith(QLatin1String("#x")) || body.startsWith(QLatin1String("#X")))
            out += QString::fromUcs4(std::array<char32_t, 1>{body.mid(2).toUInt(nullptr, 16)}.data(), 1);
        else if (body.startsWith(u'#'))
            out += QString::fromUcs4(std::array<char32_t, 1>{body.mid(1).toUInt()}.data(), 1);
        else
            out += named.value(body, match.captured(0));
    }
    out += QStringView(text).sliced(copied);
    return out;
}

// Parses a = [1,2], b = "x" into ordered name and value pairs.
QList<QPair<QString, LcValue>> parseAssignments(const QString &text)
{
    static const QRegularExpression name(QStringLiteral(R"(^([A-Za-z_]\w*)\s*=\s*)"));

    QList<QPair<QString, LcValue>> result;
    QString rest = text.trimmed();
    while (!rest.isEmpty()) {
        const auto match = name.match(rest);
        if (!match.hasMatch())
            throw LcError(QStringLiteral("expected \"name = value\" at \"%1\"").arg(rest.left(20)));

        QString after;
        LcValue value = LcValueParser::parsePrefix(rest.mid(match.capturedLength()), &after);
        result.append({match.captured(1), std::move(value)});
        rest = after.trimmed();
        if (rest.startsWith(u','))
            rest = rest.mid(1).trimmed();
        else if (!rest.isEmpty())
            throw LcError(QStringLiteral("unexpected \"%1\"").arg(rest.left(20)));
    }
    return result;
}

QList<LcValue> inputsFromTestcases(const QString &text, int perTest, QStringList *warnings)
{
    if (text.isEmpty() || perTest == 0)
        return {};

    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normalized.replace(u'\r', u'\n');
    QStringList lines = normalized.split(u'\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();

    if (lines.size() % perTest != 0) {
        warnings->append(QStringLiteral("LeetCode's example data has %1 line(s), which is not a "
                                        "multiple of %2 input(s); using the statement instead.")
                             .arg(lines.size())
                             .arg(perTest));
        return {};
    }

    QList<LcValue> values;
    try {
        for (const QString &line : std::as_const(lines))
            values.append(LcValueParser::parse(line));
    } catch (const LcError &error) {
        warnings->append(QStringLiteral("Could not parse LeetCode's example data (%1); using the "
                                        "statement instead.")
                             .arg(error.message()));
        return {};
    }
    return values;
}

ExampleBlock parseBlock(const QString &region)
{
    // Labels appear as "Output: [0,1]" in function problems, and as "Output"
    // with the value on the next line in design problems, so the colon is optional.
    const auto field = [&region](const QString &label) -> std::optional<QString> {
        const QRegularExpression start(
            QStringLiteral("(?:^|\\n)[ \\t]*%1\\b[ \\t]*:?\\s*").arg(label),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = start.match(region);
        if (!match.hasMatch())
            return std::nullopt;

        static const QRegularExpression stop(
            QStringLiteral("\\n[ \\t]*(Input|Output|Explanation|Note|Example)\\b[ \\t]*(:|\\n)|\\n\\s*\\n"),
            QRegularExpression::CaseInsensitiveOption);
        const QString after = region.mid(match.capturedEnd());
        const auto end = stop.match(after);
        QString value = (end.hasMatch() ? after.left(end.capturedStart()) : after).trimmed();
        static const QRegularExpression lineBreak(QStringLiteral(R"(\s*\n\s*)"));
        value.replace(lineBreak, QStringLiteral(" "));
        return value;
    };

    ExampleBlock block;
    block.input = field(QStringLiteral("Input"));
    block.output = field(QStringLiteral("Output"));
    return block;
}

QList<ExampleBlock> extractExampleBlocks(const QString &contentHtml)
{
    const QString text = QuestionParser::htmlToText(contentHtml);
    static const QRegularExpression header(QStringLiteral(R"(Example\s*\d*\s*:)"));
    static const QRegularExpression constraints(QStringLiteral(R"(\n\s*Constraints\s*:)"));

    QList<QRegularExpressionMatch> headers;
    auto it = header.globalMatch(text);
    while (it.hasNext())
        headers.append(it.next());

    const auto constraint = constraints.match(text);
    const qsizetype constraintsAt = constraint.hasMatch() ? constraint.capturedStart() : -1;

    QStringList regions;
    if (headers.isEmpty()) {
        regions << text;
    } else {
        for (qsizetype i = 0; i < headers.size(); ++i) {
            const qsizetype start = headers.at(i).capturedEnd();
            qsizetype end = i + 1 < headers.size() ? headers.at(i + 1).capturedStart() : text.size();
            if (i + 1 == headers.size() && constraintsAt > start)
                end = constraintsAt;
            regions << text.mid(start, end - start);
        }
    }

    QList<ExampleBlock> blocks;
    for (const QString &region : std::as_const(regions)) {
        ExampleBlock block = parseBlock(region);
        if (block.input || block.output)
            blocks.append(std::move(block));
    }
    return blocks;
}

QList<QList<LcValue>> inputsFromStatement(const QList<ExampleBlock> &blocks, const QStringList &keys,
                                          QStringList *warnings)
{
    QList<QList<LcValue>> sets;
    for (qsizetype index = 0; index < blocks.size(); ++index) {
        const ExampleBlock &block = blocks.at(index);
        if (!block.input || block.input->isEmpty())
            continue;
        try {
            const auto assignments = parseAssignments(*block.input);
            if (assignments.size() != keys.size()) {
                throw LcError(QStringLiteral("expected %1 input(s), found %2")
                                  .arg(keys.size())
                                  .arg(assignments.size()));
            }
            QList<LcValue> inputs;
            for (qsizetype i = 0; i < keys.size(); ++i) {
                const auto named = std::find_if(assignments.cbegin(), assignments.cend(),
                                                [&](const auto &a) { return a.first == keys.at(i); });
                inputs.append(named != assignments.cend() ? named->second : assignments.at(i).second);
            }
            sets.append(inputs);
        } catch (const LcError &error) {
            warnings->append(QStringLiteral("Example %1: could not parse input \"%2\" (%3).")
                                 .arg(index + 1)
                                 .arg(*block.input, error.message()));
        }
    }
    return sets;
}

// Inputs come from exampleTestcases, which holds exact data. Expected outputs
// exist only in the statement, so they are read from each example's Output line.
QList<LcTestCase> parseExamples(const ProblemMeta &meta, const QString &exampleTestcases,
                                const QString &contentHtml, QStringList *warnings)
{
    const QStringList keys = QuestionParser::inputKeys(meta);
    const QList<ExampleBlock> blocks = extractExampleBlocks(contentHtml);

    QList<QList<LcValue>> inputSets;
    const QList<LcValue> flat = inputsFromTestcases(exampleTestcases, static_cast<int>(keys.size()), warnings);
    for (qsizetype i = 0; i < flat.size(); i += keys.size())
        inputSets.append(flat.mid(i, keys.size()));

    const bool anyStatementInput = std::any_of(blocks.cbegin(), blocks.cend(), [](const ExampleBlock &b) {
        return b.input && !b.input->isEmpty();
    });
    if (inputSets.isEmpty() && anyStatementInput)
        inputSets = inputsFromStatement(blocks, keys, warnings);

    if (!blocks.isEmpty() && !inputSets.isEmpty() && blocks.size() != inputSets.size()) {
        warnings->append(QStringLiteral("Found %1 example input(s) but %2 example output(s); outputs "
                                        "were matched by position.")
                             .arg(inputSets.size())
                             .arg(blocks.size()));
    }

    QList<LcTestCase> tests;
    for (qsizetype index = 0; index < inputSets.size(); ++index) {
        LcTestCase test;
        test.id = static_cast<int>(index) + 1;
        test.name = QStringLiteral("Example %1").arg(index + 1);
        test.source = TestSource::Example;
        test.inputs = inputSets.at(index);

        if (index < blocks.size() && blocks.at(index).output) {
            const QString outputText = *blocks.at(index).output;
            test.expectedText = outputText;
            try {
                test.expected = QuestionParser::parseOutputText(outputText);
            } catch (const LcError &error) {
                warnings->append(QStringLiteral("Example %1: could not parse expected output \"%2\" (%3).")
                                     .arg(index + 1)
                                     .arg(outputText, error.message()));
            }
        } else {
            warnings->append(QStringLiteral("Example %1: no expected output found in the problem statement.")
                                 .arg(index + 1));
        }
        tests.append(std::move(test));
    }
    return tests;
}

QList<MetaParam> parseParams(const QJsonValue &params)
{
    QList<MetaParam> result;
    const QJsonArray array = params.toArray();
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QJsonObject param = array.at(i).toObject();
        const QString name = param.value(QLatin1String("name")).toString();
        result.append({name.isEmpty() ? QStringLiteral("arg%1").arg(i + 1) : name,
                       param.value(QLatin1String("type")).toString(QStringLiteral("unknown"))});
    }
    return result;
}

} // namespace

namespace QuestionParser {

QString parseProblemReference(const QString &input)
{
    const QString text = input.trimmed();
    if (text.isEmpty())
        throw LcError(QStringLiteral("Enter a LeetCode problem URL or slug, such as \"two-sum\"."));

    static const QRegularExpression url(QStringLiteral(R"((?:^|/)problems/([^/?#\s]+))"),
                                        QRegularExpression::CaseInsensitiveOption);
    const auto urlMatch = url.match(text);
    if (urlMatch.hasMatch()) {
        const QString slug = QUrl::fromPercentEncoding(urlMatch.captured(1).toUtf8()).toLower();
        if (slugPattern().match(slug).hasMatch())
            return slug;
        throw LcError(QStringLiteral("\"%1\" in the URL is not a valid problem slug.").arg(slug));
    }

    static const QRegularExpression anyUrl(QStringLiteral(R"(^https?://|leetcode\.(com|cn))"),
                                           QRegularExpression::CaseInsensitiveOption);
    if (anyUrl.match(text).hasMatch()) {
        throw LcError(QStringLiteral("That URL does not point to a problem. Use a URL like "
                                     "https://leetcode.com/problems/two-sum/"));
    }

    if (slugPattern().match(text).hasMatch()) {
        static const QRegularExpression digits(QStringLiteral(R"(^\d+$)"));
        if (digits.match(text).hasMatch()) {
            throw LcError(QStringLiteral("Problem numbers are not supported yet. Paste the problem "
                                         "URL or slug, such as \"two-sum\"."));
        }
        return text;
    }

    // Treat it as a title: "1. Two Sum" becomes "two-sum".
    QString slug = text;
    slug.remove(QRegularExpression(QStringLiteral(R"(^\d+\.\s*)")));
    slug = slug.toLower();
    slug.remove(QRegularExpression(QString::fromUtf8("['’]")));
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (slugPattern().match(slug).hasMatch())
        return slug;
    throw LcError(QStringLiteral("Could not understand \"%1\" as a LeetCode problem URL or slug.").arg(text));
}

QString problemUrl(const QString &slug, const QString &baseUrl)
{
    return QStringLiteral("%1/problems/%2/").arg(baseUrl, slug);
}

QString htmlToText(const QString &html)
{
    using Re = QRegularExpression;
    const auto ci = Re::CaseInsensitiveOption;
    QString text = html;
    text.remove(Re(QStringLiteral(R"(<(script|style)\b[\s\S]*?</\1>)"), ci));
    text.replace(Re(QStringLiteral(R"(<br\s*/?>)"), ci), QStringLiteral("\n"));
    text.replace(Re(QStringLiteral("<sup>"), ci), QStringLiteral("^"));
    text.replace(Re(QStringLiteral(R"(</(p|div|li|pre|h\d|ul|ol|tr)>)"), ci), QStringLiteral("\n"));
    text.remove(Re(QStringLiteral("<[^>]+>")));
    text = decodeEntities(text);
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(u'\r', u'\n');
    return text;
}

ProblemMeta parseMetaData(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        throw LcError(QStringLiteral("The problem metadata returned by LeetCode is not valid JSON."));
    if (!document.isObject())
        throw LcError(QStringLiteral("The problem metadata returned by LeetCode has an unexpected shape."));

    const QJsonObject data = document.object();
    ProblemMeta meta;
    meta.systemDesign = data.value(QLatin1String("systemdesign")).toBool();
    const QString plainName = data.value(QLatin1String("name")).toString();
    const QString className = data.value(QLatin1String("classname")).toString();
    meta.name = meta.systemDesign ? (className.isEmpty() ? plainName : className)
                                  : (plainName.isEmpty() ? className : plainName);
    if (meta.name.isEmpty())
        throw LcError(QStringLiteral("The problem metadata does not name a function or class."));

    meta.params = parseParams(data.value(QLatin1String("params")));
    meta.returnType = data.value(QLatin1String("return")).toObject().value(QLatin1String("type"))
                          .toString(QStringLiteral("void"));
    meta.manual = data.value(QLatin1String("manual")).toBool();

    if (meta.systemDesign) {
        meta.constructorParams =
            parseParams(data.value(QLatin1String("constructor")).toObject().value(QLatin1String("params")));
        const QJsonArray methods = data.value(QLatin1String("methods")).toArray();
        for (const QJsonValue &entry : methods) {
            const QJsonObject method = entry.toObject();
            meta.methods.append({method.value(QLatin1String("name")).toString(),
                                 parseParams(method.value(QLatin1String("params"))),
                                 method.value(QLatin1String("return")).toObject()
                                     .value(QLatin1String("type")).toString(QStringLiteral("void"))});
        }
    }

    const QJsonObject output = data.value(QLatin1String("output")).toObject();
    if (output.value(QLatin1String("paramindex")).isDouble()) {
        meta.hasOutput = true;
        meta.outputParamIndex = output.value(QLatin1String("paramindex")).toInt();
        meta.outputSize = output.value(QLatin1String("size")).toVariant().toString();
    }
    return meta;
}

QStringList inputKeys(const ProblemMeta &meta)
{
    if (meta.systemDesign)
        return {QStringLiteral("operations"), QStringLiteral("arguments")};
    QStringList keys;
    for (const MetaParam &param : meta.params)
        keys << param.name;
    return keys;
}

LcValue parseOutputText(const QString &text)
{
    QString trimmed = text.trimmed();
    if (trimmed.endsWith(u'.'))
        trimmed.chop(1);

    if (const auto whole = LcValueParser::tryParse(trimmed))
        return *whole;

    QString rest;
    LcValue value = LcValueParser::parsePrefix(trimmed, &rest);
    const QString tail = rest.trimmed();
    if (tail.startsWith(u',')) {
        const auto assignments = parseAssignments(tail.mid(1));
        if (!assignments.isEmpty()) {
            LcValue object;
            object.kind = LcValue::Kind::Object;
            object.entries.append({QStringLiteral("return"), std::move(value)});
            object.entries.append(assignments);
            return object;
        }
    }
    throw LcError(QStringLiteral("unexpected text after value: \"%1\"").arg(tail.left(30)));
}

LeetCodeProblem parseQuestion(const QJsonObject &question, const QString &baseUrl)
{
    const auto string = [&question](const char *key) {
        return question.value(QLatin1String(key)).toString();
    };

    const QString slug = string("titleSlug");
    const QString title = string("title");
    if (slug.isEmpty() || title.isEmpty()) {
        throw LcError(QStringLiteral("LeetCode returned a problem without a title or slug. The API "
                                     "may have changed."));
    }

    const QJsonArray snippets = question.value(QLatin1String("codeSnippets")).toArray();
    const bool isPaidOnly = question.value(QLatin1String("isPaidOnly")).toBool();
    if (isPaidOnly && string("content").isEmpty() && snippets.isEmpty()) {
        throw LcError(QStringLiteral("\"%1\" is a premium problem. Use LeetCode > Connect Account "
                                     "with a LeetCode Premium session to fetch it.").arg(title));
    }

    QString starterCode;
    for (const QJsonValue &snippet : snippets) {
        if (snippet.toObject().value(QLatin1String("langSlug")).toString() == QLatin1String("cpp"))
            starterCode = snippet.toObject().value(QLatin1String("code")).toString();
    }
    if (starterCode.isEmpty()) {
        throw LcError(QStringLiteral("\"%1\" has no C++ version on LeetCode (it may be a SQL, shell "
                                     "or concurrency-only problem).").arg(title));
    }
    if (string("metaData").isEmpty()) {
        throw LcError(QStringLiteral("LeetCode did not return metadata for \"%1\". The API may have "
                                     "changed.").arg(title));
    }

    LeetCodeProblem problem;
    problem.meta = parseMetaData(string("metaData"));
    problem.contentHtml = string("content");
    const QString examples = string("exampleTestcases");
    problem.examples = parseExamples(problem.meta, examples.isEmpty() ? string("sampleTestCase") : examples,
                                     problem.contentHtml, &problem.warnings);
    problem.id = string("questionId");
    problem.frontendId = string("questionFrontendId").isEmpty() ? problem.id : string("questionFrontendId");
    problem.slug = slug;
    problem.title = title;
    problem.difficulty = string("difficulty").isEmpty() ? QStringLiteral("Unknown") : string("difficulty");
    problem.isPaidOnly = isPaidOnly;
    problem.url = problemUrl(slug, baseUrl);
    problem.starterCode = starterCode;
    return problem;
}

} // namespace QuestionParser
