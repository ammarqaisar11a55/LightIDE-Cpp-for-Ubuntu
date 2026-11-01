#pragma once

#include "execution/TerminalFilter.h"

#include <QColor>
#include <QPlainTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>

// Shows a program's terminal output and turns key presses into terminal input.
// There is no local line editing: keys go to the pseudo-terminal, whose line
// discipline echoes them back, so this view only ever renders what it receives.
class TerminalView : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit TerminalView(QWidget *parent = nullptr);

    void setInputEnabled(bool enabled);
    bool inputEnabled() const { return m_inputEnabled; }

    void appendTerminalData(const QByteArray &data);
    void appendStatus(const QString &text, const QColor &color);
    void clearAll();

    // The text as displayed, for tests and copying.
    QString plainContents() const { return toPlainText(); }

signals:
    void inputReady(const QByteArray &data);
    void sizeChanged(int columns, int rows);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void putText(QStringView text);
    void putSegment(QStringView text);
    void pasteFromClipboard();
    void followOutput(bool wasAtBottom);

    TerminalFilter m_filter;
    QTextCursor m_output;   // Separate from the user's cursor so that a selection survives output.
    QTextCharFormat m_format;
    bool m_inputEnabled = false;
};
