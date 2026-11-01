#include "ui/LeetCodePanel.h"

#include "ui/Theme.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int idRole = Qt::UserRole;

struct StatusLook
{
    QString symbol;
    QString label;
    QColor color;
};

StatusLook lookOf(LcTestStatus status, const ThemeColors &colors)
{
    switch (status) {
    case LcTestStatus::Pass: return {QString::fromUtf8("✓"), LeetCodePanel::tr("Passed"), colors.success};
    case LcTestStatus::Fail: return {QString::fromUtf8("✗"), LeetCodePanel::tr("Wrong Answer"), colors.error};
    case LcTestStatus::RuntimeError: return {QString::fromUtf8("✗"), LeetCodePanel::tr("Runtime Error"), colors.error};
    case LcTestStatus::Timeout: return {QString::fromUtf8("✗"), LeetCodePanel::tr("Time Limit"), colors.warning};
    case LcTestStatus::OutputLimit: return {QString::fromUtf8("✗"), LeetCodePanel::tr("Output Limit"), colors.warning};
    case LcTestStatus::NoExpected: return {QStringLiteral("?"), LeetCodePanel::tr("No Expected Output"), colors.note};
    }
    return {};
}

void appendField(QString &text, const QString &label, const QString &value)
{
    if (!value.isEmpty())
        text += label + QStringLiteral(":\n") + value + QStringLiteral("\n\n");
}

} // namespace

LeetCodePanel::LeetCodePanel(QWidget *parent)
    : QWidget(parent)
    , m_problems(new QListWidget)
    , m_tabs(new QTabWidget)
    , m_summary(new QLabel(tr("Open a problem's solution.cpp and press Ctrl+Shift+B to run its tests.")))
    , m_results(new QTreeWidget)
    , m_details(new QPlainTextEdit)
    , m_statement(new QTextBrowser)
{
    auto *fetch = new QPushButton(tr("Fetch Problem..."));
    connect(fetch, &QPushButton::clicked, this, &LeetCodePanel::fetchRequested);
    fetch->setFocusPolicy(Qt::TabFocus);

    const auto activate = [this](QListWidgetItem *item) {
        emit problemActivated(item->data(idRole).toString());
    };
    connect(m_problems, &QListWidget::itemActivated, this, activate);
    connect(m_problems, &QListWidget::itemClicked, this, activate);

    auto *problemsArea = new QWidget;
    auto *problemsLayout = new QVBoxLayout(problemsArea);
    problemsLayout->setContentsMargins(6, 6, 6, 0);
    problemsLayout->setSpacing(6);
    problemsLayout->addWidget(fetch);
    problemsLayout->addWidget(m_problems, 1);

    m_results->setHeaderLabels({tr("Test"), tr("Result"), tr("Time")});
    m_results->setRootIsDecorated(false);
    m_results->setUniformRowHeights(true);
    m_results->setColumnWidth(0, 170);
    m_results->setColumnWidth(1, 140);
    m_results->header()->setStretchLastSection(true);
    connect(m_results, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item) { showDetails(item); });
    connect(m_results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        emit testActivated(item->data(0, idRole).toInt());
    });

    m_details->setReadOnly(true);
    m_details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto *resultsSplitter = new QSplitter(Qt::Horizontal);
    resultsSplitter->addWidget(m_results);
    resultsSplitter->addWidget(m_details);
    resultsSplitter->setStretchFactor(0, 2);
    resultsSplitter->setStretchFactor(1, 3);

    auto *resultsArea = new QWidget;
    auto *resultsLayout = new QVBoxLayout(resultsArea);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(0);
    m_summary->setContentsMargins(10, 6, 10, 6);
    m_summary->setWordWrap(true);
    resultsLayout->addWidget(m_summary);
    resultsLayout->addWidget(resultsSplitter, 1);

    m_statement->setOpenExternalLinks(true);

    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setDrawBase(false);
    m_tabs->addTab(resultsArea, tr("Test Results"));
    m_tabs->addTab(m_statement, tr("Problem Statement"));

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(problemsArea);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({230, 900});

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);
}

void LeetCodePanel::setTheme(ThemeKind theme)
{
    m_theme = theme;
}

void LeetCodePanel::setProblemsDirectory(const QString &directory)
{
    m_directory = directory;
    refreshProblems();
}

void LeetCodePanel::refreshProblems()
{
    m_problems->clear();
    const QStringList folders = QDir(m_directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : folders) {
        const ProblemFolder folder(QDir(m_directory).filePath(name));
        if (!folder.isProblem())
            continue;
        try {
            const ProblemMetadata metadata = folder.readMetadata();
            auto *item = new QListWidgetItem(metadata.id > 0
                                                 ? QStringLiteral("%1. %2").arg(metadata.id).arg(metadata.title)
                                                 : metadata.title);
            item->setData(idRole, folder.directory());
            item->setToolTip(metadata.difficulty);
            m_problems->addItem(item);
        } catch (const LcError &) {
            // A damaged folder is not a problem to offer.
        }
    }
}

void LeetCodePanel::showProblem(const ProblemFolder &folder)
{
    try {
        const ProblemMetadata metadata = folder.readMetadata();
        QFile file(folder.descriptionPath());
        const QString body = file.open(QIODevice::ReadOnly)
                                 ? QString::fromUtf8(file.readAll())
                                 : tr("<p>The statement isn't saved for this problem. Use LeetCode &gt; "
                                      "Refresh Problem to download it.</p>");
        m_statement->setHtml(QStringLiteral("<h3>%1. %2</h3><p><i>%3</i></p>%4")
                                 .arg(metadata.id)
                                 .arg(metadata.title.toHtmlEscaped(), metadata.difficulty.toHtmlEscaped(), body));
    } catch (const LcError &error) {
        m_statement->setPlainText(error.message());
    }
}

void LeetCodePanel::showStatus(const QString &text)
{
    m_summary->setStyleSheet({});
    m_summary->setText(text);
    m_tabs->setCurrentIndex(0);
}

void LeetCodePanel::clearResults()
{
    m_results->clear();
    m_details->clear();
    m_resultsById.clear();
}

void LeetCodePanel::addResult(const LcTestResult &result)
{
    const StatusLook look = lookOf(result.status, Theme::colors(m_theme));
    m_resultsById.insert(result.id, result);

    auto *item = new QTreeWidgetItem({QStringLiteral("%1  %2").arg(look.symbol, result.name), look.label,
                                      result.timeMs ? tr("%1 ms").arg(*result.timeMs, 0, 'f', 3) : QString()});
    item->setData(0, idRole, result.id);
    item->setForeground(0, look.color);
    item->setForeground(1, look.color);
    m_results->addTopLevelItem(item);

    // Open the first failure straight away; that is what a failed run is read for.
    const bool firstFailure = result.status != LcTestStatus::Pass && m_details->toPlainText().isEmpty();
    if (m_results->topLevelItemCount() == 1 || firstFailure)
        m_results->setCurrentItem(item);
}

void LeetCodePanel::showDetails(QTreeWidgetItem *item)
{
    m_details->clear();
    if (!item)
        return;
    const auto found = m_resultsById.constFind(item->data(0, idRole).toInt());
    if (found == m_resultsById.constEnd())
        return;

    const LcTestResult &result = *found;
    QString text;
    appendField(text, tr("Message"), result.message);
    appendField(text, tr("Hint"), result.hint);
    appendField(text, tr("Input"), result.input);
    appendField(text, tr("Expected"), result.expected.value_or(QString()));
    appendField(text, tr("Actual"), result.actual.value_or(QString()));
    appendField(text, tr("Your stdout"), result.stdoutText);
    appendField(text, tr("Your stderr"), result.stderrText);
    m_details->setPlainText(text.trimmed());
}

void LeetCodePanel::showReport(const LcRunReport &report, const QString &title)
{
    const ThemeColors &colors = Theme::colors(m_theme);
    m_tabs->setCurrentIndex(0);

    if (!report.error.isEmpty()) {
        m_summary->setStyleSheet(QStringLiteral("color: %1;").arg(colors.error.name()));
        m_summary->setText(report.error);
        return;
    }

    const bool allPassed = !report.failed && report.total > 0 && report.passed == report.total;
    m_summary->setStyleSheet(QStringLiteral("color: %1;")
                                 .arg((allPassed ? colors.success : report.failed ? colors.error : colors.warning).name()));
    m_summary->setText(tr("%1   %2 %3 / %4 tests passed   %5 ms")
                           .arg(title, allPassed ? QString::fromUtf8("✓") : QString::fromUtf8("✗"))
                           .arg(report.passed)
                           .arg(report.total)
                           .arg(report.totalTimeMs, 0, 'f', 3));
}

QString LeetCodePanel::summary() const
{
    return m_summary->text();
}
