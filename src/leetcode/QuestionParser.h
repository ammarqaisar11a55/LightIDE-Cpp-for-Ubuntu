#pragma once

#include "leetcode/LcProblem.h"

#include <QJsonObject>
#include <QStringList>

namespace QuestionParser {

// Extracts a problem slug from a URL, a slug or a title such as "1. Two Sum".
QString parseProblemReference(const QString &input);
QString problemUrl(const QString &slug, const QString &baseUrl = QStringLiteral("https://leetcode.com"));

// Normalizes a question object from LeetCode's GraphQL API.
LeetCodeProblem parseQuestion(const QJsonObject &question,
                              const QString &baseUrl = QStringLiteral("https://leetcode.com"));

ProblemMeta parseMetaData(const QString &json);

// Names used for a test's inputs in tests.json: parameter names, or
// operations and arguments for a design problem.
QStringList inputKeys(const ProblemMeta &meta);

// Handles single values and in-place outputs such as 2, nums = [2,2,_,_].
LcValue parseOutputText(const QString &text);

QString htmlToText(const QString &html);

} // namespace QuestionParser
