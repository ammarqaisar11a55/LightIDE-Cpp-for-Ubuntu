#include "ui/FindInFilesDialog.h"

#include "filesystem/FileManager.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int columnRole = Qt::UserRole;
constexpr int pathRole = Qt::UserRole + 1;

} // namespace

FindInFilesDialog::FindInFilesDialog(QWidget *parent)
    : QDialog(parent)
    , m_search(new FileSearch(this))
    , m_needle(new QLineEdit)
    , m_folder(new QLineEdit)
    , m_replacement(new QLineEdit)
    , m_caseSensitive(new QCheckBox(tr("Match case")))
    , m_wholeWord(new QCheckBox(tr("Whole word")))
    , m_searchButton(new QPushButton(tr("Find")))
    , m_replaceButton(new QPushButton(tr("Replace All")))
    , m_results(new QTreeWidget)
    , m_status(new QLabel)
{
    setWindowTitle(tr("Find in Files"));
    resize(760, 500);

    auto *browse = new QPushButton(tr("Browse..."));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path =
            QFileDialog::getExistingDirectory(this, tr("Search Folder"), m_folder->text());
        if (!path.isEmpty())
            m_folder->setText(path);
    });
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browse);

    auto *optionsRow = new QHBoxLayout;
    optionsRow->addWidget(m_caseSensitive);
    optionsRow->addWidget(m_wholeWord);
    optionsRow->addStretch();
    optionsRow->addWidget(m_status);
    optionsRow->addWidget(m_searchButton);

    auto *replaceRow = new QHBoxLayout;
    replaceRow->addWidget(m_replacement, 1);
    replaceRow->addWidget(m_replaceButton);

    auto *form = new QFormLayout;
    form->addRow(tr("Find:"), m_needle);
    form->addRow(tr("In folder:"), folderRow);
    form->addRow(QString(), optionsRow);
    form->addRow(tr("Replace with:"), replaceRow);

    m_results->setHeaderLabels({tr("File"), tr("Line"), tr("Text")});
    m_results->setRootIsDecorated(false);
    m_results->setUniformRowHeights(true);
    m_results->setColumnWidth(0, 220);
    m_results->setColumnWidth(1, 56);
    m_results->header()->setStretchLastSection(true);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_results, 1);

    m_replaceButton->setEnabled(false);
    m_searchButton->setDefault(true);

    connect(m_searchButton, &QPushButton::clicked, this, &FindInFilesDialog::startSearch);
    connect(m_needle, &QLineEdit::returnPressed, this, &FindInFilesDialog::startSearch);
    connect(m_replaceButton, &QPushButton::clicked, this, [this] {
        const QString replacement = m_replacement->text();
        const auto answer = QMessageBox::question(
            this, tr("Replace in Files"),
            tr("Replace every match with \"%1\"? This writes to the files on disk.").arg(replacement));
        if (answer == QMessageBox::Yes) {
            const int changed = replaceInMatchedFiles(replacement);
            m_status->setText(tr("%n file(s) changed", nullptr, changed));
        }
    });

    connect(m_search, &FileSearch::matchesFound, this, [this](const QList<FileMatch> &matches) {
        const QDir root(m_folder->text());
        for (const FileMatch &match : matches) {
            auto *item = new QTreeWidgetItem({root.relativeFilePath(match.file),
                                              QString::number(match.line), match.text});
            item->setData(0, pathRole, match.file);
            item->setData(0, columnRole, match.column);
            item->setToolTip(0, match.file);
            m_results->addTopLevelItem(item);
        }
    });
    connect(m_search, &FileSearch::searchFinished, this, &FindInFilesDialog::onFinished);

    connect(m_results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        emit matchActivated(item->data(0, pathRole).toString(), item->text(1).toInt(),
                            item->data(0, columnRole).toInt());
    });
}

void FindInFilesDialog::setFolder(const QString &folder)
{
    m_folder->setText(folder);
}

void FindInFilesDialog::setSearchText(const QString &text)
{
    m_needle->setText(text);
}

void FindInFilesDialog::startSearch()
{
    const QString needle = m_needle->text();
    if (needle.isEmpty() || !QDir(m_folder->text()).exists()) {
        m_status->setText(tr("Enter some text and an existing folder."));
        return;
    }
    m_results->clear();
    m_replaceButton->setEnabled(false);
    m_status->setText(tr("Searching..."));
    m_search->search(m_folder->text(), needle, m_caseSensitive->isChecked(),
                     m_wholeWord->isChecked());
}

void FindInFilesDialog::onFinished(int fileCount, int matchCount)
{
    m_status->setText(tr("%1 match(es) in %2 file(s) searched").arg(matchCount).arg(fileCount));
    m_replaceButton->setEnabled(matchCount > 0);
    emit searchFinished(matchCount);
}

int FindInFilesDialog::replaceInMatchedFiles(const QString &replacement)
{
    QSet<QString> files;
    for (int i = 0; i < m_results->topLevelItemCount(); ++i)
        files.insert(m_results->topLevelItem(i)->data(0, pathRole).toString());

    QString pattern = QRegularExpression::escape(m_needle->text());
    if (m_wholeWord->isChecked())
        pattern = QStringLiteral("\\b%1\\b").arg(pattern);
    const QRegularExpression expression(pattern, m_caseSensitive->isChecked()
                                                     ? QRegularExpression::NoPatternOption
                                                     : QRegularExpression::CaseInsensitiveOption);

    int changed = 0;
    for (const QString &file : std::as_const(files)) {
        LoadedFile loaded;
        QString error;
        if (!FileManager::load(file, &loaded, &error))
            continue;
        // Built by hand because QString::replace() reads \1 in the replacement as a
        // capture group, and this text is meant literally.
        QString updated;
        qsizetype copiedUpTo = 0;
        auto matches = expression.globalMatch(loaded.text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            updated += QStringView(loaded.text).sliced(copiedUpTo, match.capturedStart() - copiedUpTo);
            updated += replacement;
            copiedUpTo = match.capturedEnd();
        }
        updated += QStringView(loaded.text).sliced(copiedUpTo);

        if (updated != loaded.text && FileManager::save(file, updated, &error)) {
            ++changed;
            emit fileReplaced(file);
        }
    }

    m_results->clear();
    m_replaceButton->setEnabled(false);
    return changed;
}
