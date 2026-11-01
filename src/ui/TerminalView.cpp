#include "ui/TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollBar>
#include <QTextBlock>

namespace {

constexpr int maxLines = 20000;

// Laying out one enormous line is what would freeze the view, so a program that
// never prints a newline gets wrapped into real lines at this length.
constexpr int maxLineLength = 4096;

constexpr char interruptKey = '\x03';
constexpr char endOfFileKey = '\x04';
constexpr char suspendKey = '\x1a';
constexpr char eraseKey = '\x7f';

bool isTerminalControlKey(const QKeyEvent *event)
{
    if (event->modifiers() != Qt::ControlModifier)
        return false;
    return event->key() == Qt::Key_C || event->key() == Qt::Key_D || event->key() == Qt::Key_Z;
}

} // namespace

TerminalView::TerminalView(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_output(document())
{
    setReadOnly(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setUndoRedoEnabled(false);
    setMaximumBlockCount(maxLines);
    setLineWrapMode(WidgetWidth);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setCursorWidth(2);
}

void TerminalView::setInputEnabled(bool enabled)
{
    m_inputEnabled = enabled;
    if (enabled)
        setFocus();
}

void TerminalView::clearAll()
{
    m_filter.reset();
    clear();
    m_output = QTextCursor(document());
    m_format = QTextCharFormat();
}

void TerminalView::followOutput(bool wasAtBottom)
{
    if (!textCursor().hasSelection())
        setTextCursor(m_output);
    if (wasAtBottom)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalView::putSegment(QStringView text)
{
    // A \r earlier on the line leaves the cursor mid-line, so new text overwrites.
    if (!m_output.atBlockEnd()) {
        const int available = m_output.block().length() - 1 - m_output.positionInBlock();
        m_output.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                              std::min(available, static_cast<int>(text.size())));
    }
    m_output.insertText(text.toString(), m_format);
}

void TerminalView::putText(QStringView text)
{
    while (!text.isEmpty()) {
        const qsizetype room = maxLineLength - m_output.positionInBlock();
        if (room <= 0) {
            m_output.movePosition(QTextCursor::EndOfBlock);
            m_output.insertBlock();
            continue;
        }
        const qsizetype length = std::min(room, text.size());
        putSegment(text.first(length));
        text = text.sliced(length);
    }
}

void TerminalView::appendTerminalData(const QByteArray &data)
{
    QScrollBar *bar = verticalScrollBar();
    const bool wasAtBottom = bar->value() >= bar->maximum() - 2;

    m_output.beginEditBlock();
    const QList<TerminalFilter::Op> ops = m_filter.feed(data);
    for (const TerminalFilter::Op &op : ops) {
        switch (op.kind) {
        case TerminalFilter::Op::Kind::Text:
            putText(op.text);
            break;
        case TerminalFilter::Op::Kind::Newline:
            m_output.movePosition(QTextCursor::EndOfBlock);
            m_output.insertBlock();
            break;
        case TerminalFilter::Op::Kind::CarriageReturn:
            m_output.movePosition(QTextCursor::StartOfBlock);
            break;
        case TerminalFilter::Op::Kind::Backspace:
            if (!m_output.atBlockStart())
                m_output.movePosition(QTextCursor::Left);
            break;
        }
    }
    m_output.endEditBlock();

    followOutput(wasAtBottom);
}

void TerminalView::appendStatus(const QString &text, const QColor &color)
{
    m_filter.reset();
    m_output.movePosition(QTextCursor::End);
    if (!m_output.atBlockStart())
        m_output.insertBlock();

    QTextCharFormat format;
    format.setForeground(color);
    m_output.insertText(text, format);
    m_output.insertBlock();
    m_output.setCharFormat(QTextCharFormat());
    m_format = QTextCharFormat();
    followOutput(true);
}

bool TerminalView::event(QEvent *event)
{
    // Keep Ctrl+C and friends away from the window's shortcuts while a program
    // owns the terminal. Function keys stay with the IDE so that Stop works.
    if (event->type() == QEvent::ShortcutOverride && m_inputEnabled) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (isTerminalControlKey(key)) {
            event->accept();
            return true;
        }
    }
    return QPlainTextEdit::event(event);
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();

    if (event->matches(QKeySequence::Copy) && textCursor().hasSelection()) {
        copy();
        return;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        copy();
        return;
    }

    if (!m_inputEnabled) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }

    if (event->matches(QKeySequence::Paste)
        || (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_V)) {
        pasteFromClipboard();
        return;
    }

    if (isTerminalControlKey(event)) {
        const char control = event->key() == Qt::Key_C   ? interruptKey
                             : event->key() == Qt::Key_D ? endOfFileKey
                                                         : suspendKey;
        emit inputReady(QByteArray(1, control));
        return;
    }

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit inputReady("\n");
        return;
    case Qt::Key_Backspace:
        emit inputReady(QByteArray(1, eraseKey));
        return;
    case Qt::Key_Tab:
        emit inputReady("\t");
        return;
    default:
        break;
    }

    if (!(modifiers & (Qt::ControlModifier | Qt::MetaModifier)) && !event->text().isEmpty()) {
        emit inputReady(event->text().toUtf8());
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void TerminalView::pasteFromClipboard()
{
    QString text = QApplication::clipboard()->text();
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (!text.isEmpty())
        emit inputReady(text.toUtf8());
}

void TerminalView::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QFontMetrics metrics(font());
    const int columns = std::max(20, viewport()->width() / std::max(1, metrics.horizontalAdvance(QLatin1Char('M'))));
    const int rows = std::max(5, viewport()->height() / std::max(1, metrics.lineSpacing()));
    emit sizeChanged(columns, rows);
}

void TerminalView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();
    if (m_inputEnabled)
        menu->addAction(tr("Paste as Input"), this, &TerminalView::pasteFromClipboard);
    menu->addSeparator();
    menu->addAction(tr("Clear"), this, &TerminalView::clearAll);
    menu->exec(event->globalPos());
    delete menu;
}
