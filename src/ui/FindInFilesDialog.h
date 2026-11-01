#pragma once

#include "filesystem/FileSearch.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

class FindInFilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FindInFilesDialog(QWidget *parent = nullptr);

    void setFolder(const QString &folder);
    void setSearchText(const QString &text);
    void startSearch();

    QTreeWidget *results() const { return m_results; }
    int replaceInMatchedFiles(const QString &replacement);

signals:
    void matchActivated(const QString &file, int line, int column);

    // Emitted for each file rewritten, so that open editors can refresh.
    void fileReplaced(const QString &file);

    void searchFinished(int matchCount);

private:
    void onFinished(int fileCount, int matchCount);

    FileSearch *m_search;
    QLineEdit *m_needle;
    QLineEdit *m_folder;
    QLineEdit *m_replacement;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_wholeWord;
    QPushButton *m_searchButton;
    QPushButton *m_replaceButton;
    QTreeWidget *m_results;
    QLabel *m_status;
};
