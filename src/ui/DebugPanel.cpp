#include "ui/DebugPanel.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int maxConsoleLines = 5000;

} // namespace

DebugPanel::DebugPanel(QWidget *parent)
    : QWidget(parent)
    , m_variables(new QTreeWidget)
    , m_frames(new QTreeWidget)
    , m_console(new QPlainTextEdit)
    , m_input(new QLineEdit)
{
    m_variables->setHeaderLabels({tr("Variable"), tr("Value"), tr("Type")});
    m_variables->setRootIsDecorated(false);
    m_variables->setUniformRowHeights(true);
    m_variables->setWordWrap(false);
    m_variables->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_variables->header()->setStretchLastSection(true);
    m_variables->setColumnWidth(0, 96);
    m_variables->setColumnWidth(1, 150);

    m_frames->setHeaderLabels({tr("#"), tr("Function"), tr("Location")});
    m_frames->setRootIsDecorated(false);
    m_frames->setUniformRowHeights(true);
    m_frames->header()->setStretchLastSection(true);
    m_frames->setColumnWidth(0, 30);
    m_frames->setColumnWidth(1, 110);
    connect(m_frames, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        emit frameActivated(m_frames->indexOfTopLevelItem(item));
    });
    connect(m_frames, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        emit frameActivated(m_frames->indexOfTopLevelItem(item));
    });

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_console->setReadOnly(true);
    m_console->setFont(fixed);
    m_console->setMaximumBlockCount(maxConsoleLines);
    m_input->setFont(fixed);
    m_input->setPlaceholderText(tr("GDB command, such as: print value"));
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        const QString command = m_input->text().trimmed();
        if (command.isEmpty())
            return;
        m_input->clear();
        emit commandEntered(command);
    });

    auto *consoleArea = new QWidget;
    auto *consoleLayout = new QVBoxLayout(consoleArea);
    consoleLayout->setContentsMargins(0, 0, 0, 0);
    consoleLayout->setSpacing(0);
    consoleLayout->addWidget(m_console);
    consoleLayout->addWidget(m_input);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_variables);
    splitter->addWidget(m_frames);
    splitter->addWidget(consoleArea);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 3);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);
}

void DebugPanel::setVariables(const QList<Variable> &variables)
{
    m_variables->clear();
    for (const Variable &variable : variables) {
        auto *item = new QTreeWidgetItem({variable.name, variable.value, variable.type});
        item->setToolTip(1, variable.value);
        m_variables->addTopLevelItem(item);
    }
}

void DebugPanel::setFrames(const QList<StackFrame> &frames)
{
    m_frames->clear();
    for (const StackFrame &frame : frames) {
        const QString location = frame.file.isEmpty()
                                     ? frame.address
                                     : QStringLiteral("%1:%2").arg(QFileInfo(frame.file).fileName())
                                           .arg(frame.line);
        auto *item = new QTreeWidgetItem({QString::number(frame.level), frame.function, location});
        item->setToolTip(2, frame.file);
        m_frames->addTopLevelItem(item);
    }
    if (m_frames->topLevelItemCount() > 0)
        m_frames->setCurrentItem(m_frames->topLevelItem(0));
}

void DebugPanel::appendConsole(const QString &text)
{
    m_console->moveCursor(QTextCursor::End);
    m_console->insertPlainText(text);
    m_console->moveCursor(QTextCursor::End);
}

void DebugPanel::clearState()
{
    m_variables->clear();
    m_frames->clear();
}

void DebugPanel::clearAll()
{
    clearState();
    m_console->clear();
}

QString DebugPanel::consoleText() const
{
    return m_console->toPlainText();
}
