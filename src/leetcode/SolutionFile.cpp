#include "leetcode/SolutionFile.h"

#include "leetcode/LcValue.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace {

const QString headerPath = QStringLiteral(".lc/leetcode.hpp");

// The lookahead lets a marker end a CRLF line: unlike JavaScript, PCRE does not
// count \r as ending a line, so $ alone would not match in Windows files.
const QRegularExpression &userPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[ \t]*//[ \t]*=+[ \t]*USER SOLUTION[ \t]*=+[ \t]*(?=\r?$))"),
        QRegularExpression::MultilineOption);
    return pattern;
}

const QRegularExpression &harnessPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[ \t]*//[ \t]*=+[ \t]*TEST HARNESS[ \t]*=+[ \t]*(?=\r?$))"),
        QRegularExpression::MultilineOption);
    return pattern;
}

qsizetype lineEnd(const QString &source, qsizetype offset)
{
    if (QStringView(source).sliced(offset).startsWith(u"\r\n"))
        return offset + 2;
    return offset < source.size() && source.at(offset) == u'\n' ? offset + 1 : offset;
}

QString trimEnd(const QString &text)
{
    qsizetype end = text.size();
    while (end > 0 && text.at(end - 1).isSpace())
        --end;
    return text.left(end);
}

QString trimBlankLines(const QString &text)
{
    QString out = text;
    static const QRegularExpression leading(QStringLiteral(R"(^(\s*\n)+)"));
    out.remove(leading);
    return trimEnd(out);
}

// Puts a TODO inside the starter code's empty method bodies.
QString addTodoToEmptyBodies(const QString &code)
{
    static const QRegularExpression emptyBody(QStringLiteral(R"(\{\n([ \t]*)\n([ \t]*)\})"));
    QString out;
    qsizetype copied = 0;
    auto it = emptyBody.globalMatch(code);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString inner = match.captured(1);
        const QString closing = match.captured(2);
        const QString indent = inner.size() > closing.size() ? inner : closing + QStringLiteral("    ");
        out += QStringView(code).sliced(copied, match.capturedStart() - copied);
        out += QStringLiteral("{\n%1// TODO: write your solution\n%2}").arg(indent, closing);
        copied = match.capturedEnd();
    }
    out += QStringView(code).sliced(copied);
    return out;
}

const QList<CommentedDefinition> &defaultDefinitions()
{
    static const QList<CommentedDefinition> definitions = {
        {QStringLiteral("ListNode"),
         QStringLiteral("struct ListNode {\n"
                        "    int val;\n"
                        "    ListNode *next;\n"
                        "    ListNode() : val(0), next(nullptr) {}\n"
                        "    ListNode(int x) : val(x), next(nullptr) {}\n"
                        "    ListNode(int x, ListNode *next) : val(x), next(next) {}\n"
                        "};")},
        {QStringLiteral("TreeNode"),
         QStringLiteral("struct TreeNode {\n"
                        "    int val;\n"
                        "    TreeNode *left;\n"
                        "    TreeNode *right;\n"
                        "    TreeNode() : val(0), left(nullptr), right(nullptr) {}\n"
                        "    TreeNode(int x) : val(x), left(nullptr), right(nullptr) {}\n"
                        "    TreeNode(int x, TreeNode *left, TreeNode *right) : val(x), left(left), right(right) {}\n"
                        "};")},
    };
    return definitions;
}

} // namespace

namespace SolutionFile {

const QString userSolutionMarker = QStringLiteral("// ==================== USER SOLUTION ====================");
const QString testHarnessMarker = QStringLiteral("// ==================== TEST HARNESS ======================");

std::optional<Sections> split(const QString &source)
{
    const auto user = userPattern().match(source);
    const auto harness = harnessPattern().match(source);
    if (!user.hasMatch() || !harness.hasMatch() || harness.capturedStart() < user.capturedStart())
        return std::nullopt;

    const qsizetype solutionStart = lineEnd(source, user.capturedEnd());
    Sections sections;
    sections.harnessStart = lineEnd(source, harness.capturedEnd());
    sections.preamble = source.left(user.capturedStart());
    sections.solution = source.mid(solutionStart, harness.capturedStart() - solutionStart);
    sections.harness = source.mid(sections.harnessStart);
    return sections;
}

QString replaceHarness(const QString &source, const QString &generatedHarness)
{
    // Match the file's line endings so that an unchanged harness compares equal.
    QString harnessCode = generatedHarness;
    if (source.contains(QLatin1String("\r\n")))
        harnessCode.replace(QRegularExpression(QStringLiteral("\\r?\\n")), QStringLiteral("\r\n"));

    if (const auto sections = split(source))
        return source.left(sections->harnessStart) + harnessCode;

    if (!userPattern().match(source).hasMatch()) {
        throw LcError(QStringLiteral("solution.cpp has no \"USER SOLUTION\" marker, so your code "
                                     "cannot be told apart from the test harness. Add this line "
                                     "above your class:\n%1").arg(userSolutionMarker));
    }
    return trimEnd(source) + QStringLiteral("\n\n") + testHarnessMarker + QLatin1Char('\n') + harnessCode;
}

QString extractSubmission(const QString &source)
{
    QString solution;
    if (const auto sections = split(source)) {
        solution = sections->solution;
    } else {
        const auto user = userPattern().match(source);
        if (!user.hasMatch()) {
            throw LcError(QStringLiteral("Could not find the \"USER SOLUTION\" marker in "
                                         "solution.cpp. Restore this line above your class:\n%1")
                              .arg(userSolutionMarker));
        }
        solution = source.mid(lineEnd(source, user.capturedEnd()));
    }
    return trimBlankLines(stripLocalOnly(solution)) + QLatin1Char('\n');
}

QString stripLocalOnly(const QString &code)
{
    struct Frame
    {
        bool local;
        bool inElse;
    };

    static const QRegularExpression directive(QStringLiteral(R"(^\s*#\s*(\w+)(.*)$)"));
    static const QRegularExpression ifdefLocal(QStringLiteral(R"(^\s*LC_LOCAL\b)"));
    static const QRegularExpression ifDefinedLocal(QStringLiteral(R"(^\s*defined\s*\(?\s*LC_LOCAL\b)"));
    static const QRegularExpression localLine(QStringLiteral(R"(//\s*@local\b)"));
    static const QRegularExpression lineBreak(QStringLiteral("\\r?\\n"));

    QList<Frame> stack;
    QStringList out;
    const auto keeping = [&stack] {
        return std::all_of(stack.cbegin(), stack.cend(),
                           [](const Frame &f) { return !f.local || f.inElse; });
    };

    const QStringList lines = code.split(lineBreak);
    for (const QString &line : lines) {
        const auto match = directive.match(line);
        if (match.hasMatch()) {
            const QString name = match.captured(1);
            const QString rest = match.captured(2);
            if (name == u"ifdef" || name == u"if" || name == u"ifndef") {
                const bool isLocal = name == u"ifdef" ? ifdefLocal.match(rest).hasMatch()
                                     : name == u"if"  ? ifDefinedLocal.match(rest).hasMatch()
                                                      : false;
                stack.append({isLocal, false});
                if (isLocal)
                    continue;
            } else if ((name == u"else" || name == u"elif") && !stack.isEmpty()) {
                if (stack.last().local) {
                    stack.last().inElse = true;
                    continue;
                }
            } else if (name == u"endif" && !stack.isEmpty()) {
                const Frame top = stack.takeLast();
                if (top.local)
                    continue;
            }
        }
        if (keeping() && !localLine.match(line).hasMatch())
            out << line;
    }
    return out.join(QLatin1Char('\n'));
}

QString generate(const Template &file)
{
    QString starter = file.starterCode;
    starter.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    starter.replace(u'\r', u'\n');

    const QStringList lines = {
        QStringLiteral("// %1. %2 (%3)").arg(file.frontendId, file.title, file.difficulty),
        QStringLiteral("// ") + file.url,
        QStringLiteral("//"),
        QStringLiteral("// Write your solution in the USER SOLUTION section. Only that section is copied by"),
        QStringLiteral("// \"LeetCode: Copy Solution\". Run the tests with Ctrl+Shift+B (LeetCode: Run Tests)."),
        QString(),
        QStringLiteral("#include \"%1\"  // local only: standard library, LeetCode types, test helpers").arg(headerPath),
        QString(),
        userSolutionMarker,
        QString(),
        addTodoToEmptyBodies(trimEnd(starter)),
        QString(),
        testHarnessMarker,
        file.harnessCode,
    };
    return lines.join(QLatin1Char('\n'));
}

QList<CommentedDefinition> requiredDefinitions(const CppStarter &starter)
{
    const auto definedInCode = [&starter](const QString &name) {
        return std::any_of(starter.classes.cbegin(), starter.classes.cend(),
                           [&name](const CppClass &c) { return c.name == name; });
    };

    QList<CommentedDefinition> definitions;
    for (const CommentedDefinition &definition : starter.commentedDefinitions) {
        if (!definedInCode(definition.name))
            definitions.append(definition);
    }

    QString code = starter.code;
    code.remove(QRegularExpression(QStringLiteral(R"(/\*[\s\S]*?\*/)")));
    code.remove(QRegularExpression(QStringLiteral(R"(//.*$)"), QRegularExpression::MultilineOption));

    for (const CommentedDefinition &definition : defaultDefinitions()) {
        const bool used = code.contains(QRegularExpression(QStringLiteral(R"(\b%1\b)").arg(definition.name)));
        const bool present = std::any_of(definitions.cbegin(), definitions.cend(), [&](const CommentedDefinition &d) {
            return d.name == definition.name;
        });
        if (used && !present && !definedInCode(definition.name))
            definitions.append(definition);
    }
    return definitions;
}

QString generateHeader(const QString &runtimeSource, const QList<CommentedDefinition> &definitions)
{
    QStringList parts = {trimEnd(runtimeSource), QString()};
    if (!definitions.isEmpty()) {
        parts << QStringLiteral("// ---------------------------------------------------------------------------")
              << QStringLiteral("// Types LeetCode provides for this problem (taken from its starter code)")
              << QStringLiteral("// ---------------------------------------------------------------------------")
              << QString();
        for (const CommentedDefinition &definition : definitions)
            parts << definition.code << QString();
    }
    return parts.join(QLatin1Char('\n'));
}

} // namespace SolutionFile
