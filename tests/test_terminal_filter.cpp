#include "execution/TerminalFilter.h"

#include <QtTest>

namespace {

// Renders ops as readable text: \n, <CR> and <BS> for the control operations.
QString render(const QList<TerminalFilter::Op> &ops)
{
    QString out;
    for (const auto &op : ops) {
        switch (op.kind) {
        case TerminalFilter::Op::Kind::Text: out += op.text; break;
        case TerminalFilter::Op::Kind::Newline: out += "\n"; break;
        case TerminalFilter::Op::Kind::CarriageReturn: out += "<CR>"; break;
        case TerminalFilter::Op::Kind::Backspace: out += "<BS>"; break;
        }
    }
    return out;
}

} // namespace

class TestTerminalFilter : public QObject
{
    Q_OBJECT

private slots:
    void crLfBecomesNewline()
    {
        TerminalFilter filter;
        QCOMPARE(render(filter.feed("one\r\ntwo\r\n")), QString("one\ntwo\n"));
    }

    void crLfSplitAcrossReads()
    {
        TerminalFilter filter;
        QString out = render(filter.feed("one\r"));
        out += render(filter.feed("\ntwo"));
        QCOMPARE(out, QString("one\ntwo"));
    }

    void loneCarriageReturnRewinds()
    {
        TerminalFilter filter;
        QCOMPARE(render(filter.feed("10%\r20%")), QString("10%<CR>20%"));
    }

    void backspaceFromLineEditing()
    {
        TerminalFilter filter;
        QCOMPARE(render(filter.feed("ab\b \b")), QString("ab<BS> <BS>"));
    }

    void stripsColourSequences()
    {
        TerminalFilter filter;
        QCOMPARE(render(filter.feed("\x1b[1;31mred\x1b[0m plain")), QString("red plain"));
    }

    void escapeSequenceSplitAcrossReads()
    {
        TerminalFilter filter;
        QString out = render(filter.feed("a\x1b["));
        out += render(filter.feed("31"));
        out += render(filter.feed("mb"));
        QCOMPARE(out, QString("ab"));
    }

    void stripsOscTitleSequences()
    {
        TerminalFilter filter;
        QCOMPARE(render(filter.feed("\x1b]0;title\x07text")), QString("text"));
        QCOMPARE(render(filter.feed("\x1b]8;;http://a\x1b\\link")), QString("link"));
    }

    void utf8SplitAcrossReads()
    {
        const QByteArray bytes = QString::fromUtf8("héllo").toUtf8();
        TerminalFilter filter;
        QString out = render(filter.feed(bytes.left(2)));
        out += render(filter.feed(bytes.mid(2)));
        QCOMPARE(out, QString::fromUtf8("héllo"));
    }

    void keepsTabsAndDropsOtherControls()
    {
        TerminalFilter filter;
        QCOMPARE(render(filter.feed("a\tb\x07""c\x01""d")), QString("a\tbcd"));
    }

    void resetClearsState()
    {
        TerminalFilter filter;
        filter.feed("\x1b[");
        filter.reset();
        QCOMPARE(render(filter.feed("31m")), QString("31m"));
    }
};

QTEST_APPLESS_MAIN(TestTerminalFilter)
#include "test_terminal_filter.moc"
