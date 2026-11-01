#pragma once

#include <QList>
#include <QString>
#include <QThread>

#include <atomic>

struct FileMatch
{
    QString file;
    int line = 0;     // 1-based.
    int column = 0;   // 1-based.
    QString text;     // The matching line, trimmed.
};

// Find in Files. Runs on its own thread because it is the one CPU-bound job the
// IDE has; batches of results arrive on the receiver's thread.
class FileSearch : public QThread
{
    Q_OBJECT

public:
    explicit FileSearch(QObject *parent = nullptr);
    ~FileSearch() override;

    void search(const QString &root, const QString &needle, bool caseSensitive, bool wholeWord);
    void cancel() { m_cancelled = true; }

signals:
    void matchesFound(const QList<FileMatch> &matches);
    void searchFinished(int fileCount, int matchCount);

protected:
    void run() override;

private:
    QString m_root;
    QString m_needle;
    bool m_caseSensitive = false;
    bool m_wholeWord = false;
    std::atomic<bool> m_cancelled{false};
};
