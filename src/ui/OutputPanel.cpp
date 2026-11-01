#include "ui/OutputPanel.h"

#include "compiler/DiagnosticsParser.h"
#include "ui/DebugPanel.h"
#include "ui/TerminalView.h"
#include "ui/Theme.h"

#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QTextBlock>
#include <QTreeWidget>

namespace {

constexpr int maxCompilerLines = 20000;

QString severityName(Diagnostic::Severity severity)
{
    switch (severity) {
    case Diagnostic::Severity::Error: return OutputPanel::tr("Error");
    case Diagnostic::Severity::Warning: return OutputPanel::tr("Warning");
    case Diagnostic::Severity::Note: break;
    }
    return OutputPanel::tr("Note");
}

} // namespace

OutputPanel::OutputPanel(QWidget *parent)
    : QTabWidget(parent)
    , m_compiler(new QPlainTextEdit)
    , m_messages(new QTreeWidget)
    , m_terminal(new TerminalView)
    , m_debug(new DebugPanel)
{
    setDocumentMode(true);
    tabBar()->setDrawBase(false);

    m_compiler->setReadOnly(true);
    m_compiler->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_compiler->setMaximumBlockCount(maxCompilerLines);
    m_compiler->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_compiler->viewport()->installEventFilter(this);
    m_compiler->viewport()->setCursor(Qt::PointingHandCursor);

    m_messages->setHeaderLabels({tr("Severity"), tr("File"), tr("Line"), tr("Message")});
    m_messages->setRootIsDecorated(false);
    m_messages->setUniformRowHeights(true);
    m_messages->setColumnWidth(0, 80);
    m_messages->setColumnWidth(1, 200);
    m_messages->setColumnWidth(2, 60);
    m_messages->header()->setStretchLastSection(true);

    const auto activate = [this](QTreeWidgetItem *item) {
        const int row = m_messages->indexOfTopLevelItem(item);
        if (row >= 0 && row < m_diagnostics.size())
            emit diagnosticActivated(m_diagnostics.at(row));
    };
    connect(m_messages, &QTreeWidget::itemActivated, this, activate);
    connect(m_messages, &QTreeWidget::itemClicked, this, activate);

    addTab(m_compiler, tr("Compiler Output"));
    addTab(m_messages, tr("Build Messages"));
    addTab(m_terminal, tr("Application Output"));
    addTab(m_debug, tr("Debug Console"));
}

void OutputPanel::setTheme(ThemeKind theme)
{
    m_theme = theme;
}

void OutputPanel::clearBuild()
{
    m_compiler->clear();
    m_messages->clear();
    m_diagnostics.clear();
    m_errors = 0;
    m_warnings = 0;
    updateMessagesTabTitle();
}

void OutputPanel::appendColored(const QString &text, const QColor &color)
{
    QTextCharFormat format;
    format.setForeground(color);
    QTextCursor cursor(m_compiler->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text + QLatin1Char('\n'), format);
    m_compiler->moveCursor(QTextCursor::End);
    m_compiler->ensureCursorVisible();
}

void OutputPanel::appendCommand(const QString &command)
{
    appendColored(command, Theme::colors(m_theme).textMuted);
}

void OutputPanel::appendOutputLine(const QString &line)
{
    const ThemeColors &colors = Theme::colors(m_theme);
    QColor color = colors.text;
    if (const auto diagnostic = DiagnosticsParser::parseLine(line)) {
        switch (diagnostic->severity) {
        case Diagnostic::Severity::Error: color = colors.error; break;
        case Diagnostic::Severity::Warning: color = colors.warning; break;
        case Diagnostic::Severity::Note: color = colors.note; break;
        }
    }
    appendColored(line, color);
}

void OutputPanel::appendBuildStatus(const QString &text, bool success)
{
    const ThemeColors &colors = Theme::colors(m_theme);
    appendColored(text, success ? colors.success : colors.error);
}

void OutputPanel::addDiagnostic(const Diagnostic &diagnostic)
{
    m_diagnostics.append(diagnostic);

    const ThemeColors &colors = Theme::colors(m_theme);
    QColor color = colors.note;
    QString iconName = QStringLiteral("note");
    if (diagnostic.severity == Diagnostic::Severity::Error) {
        color = colors.error;
        iconName = QStringLiteral("error");
        ++m_errors;
    } else if (diagnostic.severity == Diagnostic::Severity::Warning) {
        color = colors.warning;
        iconName = QStringLiteral("warning");
        ++m_warnings;
    }

    auto *item = new QTreeWidgetItem({severityName(diagnostic.severity),
                                      QFileInfo(diagnostic.file).fileName(),
                                      diagnostic.line > 0 ? QString::number(diagnostic.line)
                                                          : QString(),
                                      diagnostic.message});
    item->setIcon(0, Theme::icon(iconName, color));
    item->setToolTip(1, diagnostic.file);
    item->setToolTip(3, diagnostic.message);
    m_messages->addTopLevelItem(item);
    updateMessagesTabTitle();
}

void OutputPanel::updateMessagesTabTitle()
{
    const int count = m_errors + m_warnings;
    setTabText(MessagesTab, count > 0 ? tr("Build Messages (%1)").arg(count) : tr("Build Messages"));
}

bool OutputPanel::activateCompilerLine(int blockNumber)
{
    const QTextBlock block = m_compiler->document()->findBlockByNumber(blockNumber);
    if (!block.isValid())
        return false;
    const auto diagnostic = DiagnosticsParser::parseLine(block.text());
    if (!diagnostic || !diagnostic->hasLocation())
        return false;
    emit diagnosticActivated(*diagnostic);
    return true;
}

bool OutputPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_compiler->viewport() && event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        // A drag selects text to copy; only a plain click navigates.
        if (mouse->button() == Qt::LeftButton && !m_compiler->textCursor().hasSelection()) {
            const QTextCursor cursor = m_compiler->cursorForPosition(mouse->position().toPoint());
            activateCompilerLine(cursor.blockNumber());
        }
    }
    return QTabWidget::eventFilter(watched, event);
}
