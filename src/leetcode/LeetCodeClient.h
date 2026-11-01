#pragma once

#include "leetcode/LcProblem.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Reads problems from LeetCode's public GraphQL endpoint, the one its own
// problem page uses. Public problems need no account. A session the user chose
// to share is attached for Premium problems, and is only ever held in memory.
// This makes plain requests only, nothing that gets around bot protection.
class LeetCodeClient : public QObject
{
    Q_OBJECT

public:
    explicit LeetCodeClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url);
    void setSession(const QString &session, const QString &csrfToken = QString());
    bool hasSession() const { return !m_session.isEmpty(); }

    void fetchProblem(const QString &slug);
    bool isBusy() const { return m_reply != nullptr; }

signals:
    void problemFetched(const LeetCodeProblem &problem);
    void fetchFailed(const QString &message);

private:
    void onFinished(const QString &slug);
    QNetworkAccessManager *network();

    QNetworkAccessManager *m_network = nullptr;   // Created on first use, as it loads TLS.
    QNetworkReply *m_reply = nullptr;
    QString m_baseUrl = QStringLiteral("https://leetcode.com");
    QString m_session;
    QString m_csrfToken;
};
