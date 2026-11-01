#include "leetcode/LeetCodeClient.h"

#include "leetcode/QuestionParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

constexpr int timeoutMs = 20000;

const char *const questionQuery = R"(query questionData($titleSlug: String!) {
  question(titleSlug: $titleSlug) {
    questionId
    questionFrontendId
    title
    titleSlug
    difficulty
    isPaidOnly
    content
    exampleTestcases
    sampleTestCase
    metaData
    codeSnippets { lang langSlug code }
  }
})";

} // namespace

LeetCodeClient::LeetCodeClient(QObject *parent)
    : QObject(parent)
{
}

QNetworkAccessManager *LeetCodeClient::network()
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    return m_network;
}

void LeetCodeClient::setBaseUrl(const QString &url)
{
    m_baseUrl = url;
    while (m_baseUrl.endsWith(u'/'))
        m_baseUrl.chop(1);
}

void LeetCodeClient::setSession(const QString &session, const QString &csrfToken)
{
    m_session = session;
    m_csrfToken = csrfToken;
}

void LeetCodeClient::fetchProblem(const QString &slug)
{
    if (m_reply)
        return;

    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/graphql/")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Referer", QStringLiteral("%1/problems/%2/").arg(m_baseUrl, slug).toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LightIDE"));
    request.setTransferTimeout(timeoutMs);

    if (!m_session.isEmpty()) {
        QByteArray cookies = "LEETCODE_SESSION=" + m_session.toUtf8();
        if (!m_csrfToken.isEmpty()) {
            cookies += "; csrftoken=" + m_csrfToken.toUtf8();
            request.setRawHeader("x-csrftoken", m_csrfToken.toUtf8());
        }
        request.setRawHeader("Cookie", cookies);
    }

    const QJsonObject body{{QStringLiteral("operationName"), QStringLiteral("questionData")},
                           {QStringLiteral("query"), QString::fromUtf8(questionQuery)},
                           {QStringLiteral("variables"), QJsonObject{{QStringLiteral("titleSlug"), slug}}}};

    m_reply = network()->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, [this, slug] { onFinished(slug); });
}

void LeetCodeClient::onFinished(const QString &slug)
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();

    if (status == 0) {
        emit fetchFailed(reply->error() == QNetworkReply::OperationCanceledError
                             ? tr("LeetCode did not respond within %1 s.").arg(timeoutMs / 1000)
                             : tr("Could not reach LeetCode: %1\nCheck your internet connection and "
                                  "try again.").arg(reply->errorString()));
        return;
    }
    if (status == 401 || (status == 403 && !m_session.isEmpty())) {
        emit fetchFailed(tr("LeetCode rejected the saved session, which may have expired. Use "
                            "LeetCode > Connect Account again, or disconnect to fetch public "
                            "problems anonymously."));
        return;
    }
    if (status == 403 || status == 429) {
        emit fetchFailed(tr("LeetCode refused the request (HTTP %1); it may be rate-limiting or "
                            "blocking automated requests. Wait a minute and try again, and open the "
                            "problem in your browser to confirm leetcode.com is reachable.").arg(status));
        return;
    }
    if (status < 200 || status >= 300) {
        emit fetchFailed(tr("LeetCode returned HTTP %1. Try again later.").arg(status));
        return;
    }

    QJsonParseError parseError;
    const QJsonObject root = QJsonDocument::fromJson(data, &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        emit fetchFailed(tr("LeetCode returned a response that is not JSON. The API may have changed."));
        return;
    }

    const QJsonArray errors = root.value(QLatin1String("errors")).toArray();
    if (!errors.isEmpty()) {
        QStringList messages;
        for (const QJsonValue &error : errors)
            messages << error.toObject().value(QLatin1String("message")).toString(tr("unknown error"));
        emit fetchFailed(tr("LeetCode API error: %1").arg(messages.join(QStringLiteral("; "))));
        return;
    }

    if (!root.value(QLatin1String("data")).isObject()) {
        emit fetchFailed(tr("LeetCode returned no data. The API may have changed."));
        return;
    }

    const QJsonValue question = root.value(QLatin1String("data")).toObject().value(QLatin1String("question"));
    if (!question.isObject()) {
        emit fetchFailed(tr("LeetCode has no problem with the slug \"%1\". Check the URL or slug, such "
                            "as \"two-sum\" from https://leetcode.com/problems/two-sum/.").arg(slug));
        return;
    }

    try {
        emit problemFetched(QuestionParser::parseQuestion(question.toObject(), m_baseUrl));
    } catch (const LcError &error) {
        emit fetchFailed(error.message());
    }
}
