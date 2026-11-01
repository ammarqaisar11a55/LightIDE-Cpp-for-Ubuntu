#include "filesystem/FileSearch.h"

#include "filesystem/FileManager.h"

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>

namespace {

constexpr qint64 maxSearchedFileSize = 4 * 1024 * 1024;
constexpr int batchSize = 64;

bool inSkippedDirectory(const QString &relativePath)
{
    const auto parts = QStringView(relativePath).split(u'/');
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        if (parts.at(i).startsWith(u'.') || parts.at(i) == u"build")
            return true;
    }
    return false;
}

} // namespace

FileSearch::FileSearch(QObject *parent)
    : QThread(parent)
{
}

FileSearch::~FileSearch()
{
    cancel();
    wait();
}

void FileSearch::search(const QString &root, const QString &needle, bool caseSensitive,
                        bool wholeWord)
{
    cancel();
    wait();
    m_root = root;
    m_needle = needle;
    m_caseSensitive = caseSensitive;
    m_wholeWord = wholeWord;
    m_cancelled = false;
    start();
}

void FileSearch::run()
{
    QString pattern = QRegularExpression::escape(m_needle);
    if (m_wholeWord)
        pattern = QStringLiteral("\\b%1\\b").arg(pattern);
    QRegularExpression expression(pattern, m_caseSensitive
                                               ? QRegularExpression::NoPatternOption
                                               : QRegularExpression::CaseInsensitiveOption);

    QList<FileMatch> batch;
    int fileCount = 0;
    int matchCount = 0;
    const QDir root(m_root);

    QDirIterator it(m_root, FileManager::supportedPatterns(), QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && !m_cancelled) {
        const QString path = it.next();
        if (inSkippedDirectory(root.relativeFilePath(path)) || it.fileInfo().size() > maxSearchedFileSize)
            continue;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(file.readAll());
        ++fileCount;

        int lineNumber = 0;
        const auto lines = QStringView(text).split(u'\n');
        for (QStringView line : lines) {
            ++lineNumber;
            const auto match = expression.match(line.toString());
            if (!match.hasMatch())
                continue;
            batch.append(FileMatch{path, lineNumber, static_cast<int>(match.capturedStart()) + 1,
                                   line.trimmed().toString()});
            ++matchCount;
            if (batch.size() >= batchSize) {
                emit matchesFound(batch);
                batch.clear();
            }
        }
    }

    if (!batch.isEmpty())
        emit matchesFound(batch);
    emit searchFinished(fileCount, matchCount);
}
