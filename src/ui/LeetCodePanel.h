#pragma once

#include "core/Settings.h"
#include "leetcode/TestSuiteRunner.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

// The LeetCode tab of the bottom panel: problems on disk, the latest test
// results, and the statement of the current problem.
class LeetCodePanel : public QWidget
{
    Q_OBJECT

public:
    explicit LeetCodePanel(QWidget *parent = nullptr);

    void setTheme(ThemeKind theme);
    void setProblemsDirectory(const QString &directory);
    void refreshProblems();
    void showProblem(const ProblemFolder &folder);

    void showStatus(const QString &text);
    void clearResults();
    void addResult(const LcTestResult &result);
    void showReport(const LcRunReport &report, const QString &title);

    QListWidget *problems() const { return m_problems; }
    QTreeWidget *results() const { return m_results; }
    QPlainTextEdit *details() const { return m_details; }
    QTextBrowser *statement() const { return m_statement; }
    QString summary() const;

signals:
    void fetchRequested();
    void problemActivated(const QString &directory);
    void testActivated(int id);

private:
    void showDetails(QTreeWidgetItem *item);

    QListWidget *m_problems;
    QTabWidget *m_tabs;
    QLabel *m_summary;
    QTreeWidget *m_results;
    QPlainTextEdit *m_details;
    QTextBrowser *m_statement;
    QString m_directory;
    QHash<int, LcTestResult> m_resultsById;
    ThemeKind m_theme = ThemeKind::Dark;
};
