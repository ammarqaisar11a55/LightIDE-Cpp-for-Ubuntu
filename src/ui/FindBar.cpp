#include "ui/FindBar.h"

#include "editor/CodeEditor.h"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
    , m_findEdit(new QLineEdit)
    , m_replaceEdit(new QLineEdit)
    , m_replaceRow(new QWidget)
    , m_caseSensitive(new QCheckBox(tr("Match case")))
    , m_wholeWord(new QCheckBox(tr("Whole word")))
    , m_regex(new QCheckBox(tr("Regex")))
    , m_status(new QLabel)
{
    setObjectName(QStringLiteral("findBar"));
    setAttribute(Qt::WA_StyledBackground);

    m_findEdit->setPlaceholderText(tr("Find"));
    m_findEdit->setClearButtonEnabled(true);
    m_replaceEdit->setPlaceholderText(tr("Replace with"));
    m_findEdit->installEventFilter(this);
    m_replaceEdit->installEventFilter(this);

    auto *previous = new QPushButton(tr("Previous"));
    auto *next = new QPushButton(tr("Next"));
    auto *close = new QPushButton(tr("Close"));
    auto *replace = new QPushButton(tr("Replace"));
    auto *replaceEverywhere = new QPushButton(tr("Replace All"));
    for (QPushButton *button : {previous, next, close, replace, replaceEverywhere})
        button->setFocusPolicy(Qt::TabFocus);

    auto *findRow = new QHBoxLayout;
    findRow->setSpacing(6);
    findRow->addWidget(m_findEdit, 1);
    findRow->addWidget(previous);
    findRow->addWidget(next);
    findRow->addWidget(m_caseSensitive);
    findRow->addWidget(m_wholeWord);
    findRow->addWidget(m_regex);
    findRow->addWidget(m_status);
    findRow->addStretch();
    findRow->addWidget(close);

    auto *replaceRow = new QHBoxLayout(m_replaceRow);
    replaceRow->setContentsMargins(0, 0, 0, 0);
    replaceRow->setSpacing(6);
    replaceRow->addWidget(m_replaceEdit, 1);
    replaceRow->addWidget(replace);
    replaceRow->addWidget(replaceEverywhere);
    replaceRow->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);
    layout->addLayout(findRow);
    layout->addWidget(m_replaceRow);

    connect(next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(previous, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    connect(replace, &QPushButton::clicked, this, &FindBar::replaceCurrent);
    connect(replaceEverywhere, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(m_findEdit, &QLineEdit::textChanged, this, &FindBar::onSearchTextChanged);
    connect(m_findEdit, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(m_replaceEdit, &QLineEdit::returnPressed, this, &FindBar::replaceCurrent);

    hide();
}

void FindBar::setEditor(CodeEditor *editor)
{
    m_editor = editor;
    m_status->clear();
}

void FindBar::setSearchText(const QString &text)
{
    m_findEdit->setText(text);
}

void FindBar::setReplaceText(const QString &text)
{
    m_replaceEdit->setText(text);
}

void FindBar::reveal(bool withReplace)
{
    if (m_editor && m_editor->hasSelectedText()) {
        const QString selection = m_editor->selectedText();
        if (!selection.contains(QLatin1Char('\n')) && !selection.contains(QLatin1Char('\r'))) {
            const bool blocked = m_findEdit->blockSignals(true);
            m_findEdit->setText(selection);
            m_findEdit->blockSignals(blocked);
        }
    }
    m_replaceRow->setVisible(withReplace);
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void FindBar::showFind()
{
    reveal(false);
}

void FindBar::showReplace()
{
    reveal(true);
}

bool FindBar::find(bool forward, bool fromSelectionStart)
{
    if (!m_editor || m_findEdit->text().isEmpty()) {
        m_status->clear();
        return false;
    }

    int line = -1;
    int index = -1;
    if (!forward || fromSelectionStart) {
        // QScintilla starts a search at the caret, which sits after the current
        // match. Starting at its beginning keeps searching in place, and going
        // backwards past it finds the previous match.
        int lineTo, indexTo;
        m_editor->getSelection(&line, &index, &lineTo, &indexTo);
    }

    const bool found = m_editor->findFirst(m_findEdit->text(), m_regex->isChecked(),
                                           m_caseSensitive->isChecked(), m_wholeWord->isChecked(),
                                           true, forward, line, index, true, false, true);
    m_status->setText(found ? QString() : tr("No results"));
    return found;
}

void FindBar::onSearchTextChanged()
{
    find(true, true);
}

bool FindBar::findNext()
{
    return find(true, false);
}

bool FindBar::findPrevious()
{
    return find(false, false);
}

void FindBar::replaceCurrent()
{
    if (!m_editor)
        return;
    // Replace acts on the highlighted match; if there is none, find one first.
    if (!m_editor->hasSelectedText()) {
        findNext();
        return;
    }
    m_editor->replace(m_replaceEdit->text());
    findNext();
}

int FindBar::replaceAll()
{
    if (!m_editor || m_findEdit->text().isEmpty())
        return 0;

    int count = 0;
    int line, index;
    m_editor->getCursorPosition(&line, &index);

    m_editor->beginUndoAction();
    bool found = m_editor->findFirst(m_findEdit->text(), m_regex->isChecked(),
                                     m_caseSensitive->isChecked(), m_wholeWord->isChecked(), false,
                                     true, 0, 0, false, false, true);
    while (found) {
        m_editor->replace(m_replaceEdit->text());
        ++count;
        found = m_editor->findNext();
    }
    m_editor->endUndoAction();

    m_editor->setCursorPosition(line, index);
    m_status->setText(tr("%n replaced", nullptr, count));
    return count;
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            hide();
            if (m_editor)
                m_editor->setFocus();
            return true;
        }
        if (watched == m_findEdit && key->key() == Qt::Key_Return
            && (key->modifiers() & Qt::ShiftModifier)) {
            findPrevious();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
