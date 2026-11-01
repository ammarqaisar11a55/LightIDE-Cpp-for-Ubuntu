#include "editor/CodeEditor.h"

#include "editor/SyntaxHighlighter.h"
#include "ui/Theme.h"

#include <Qsci/qsciapis.h>

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMimeData>
#include <QToolTip>

#include <algorithm>

namespace {

constexpr int dwellTimeMs = 450;
constexpr int completionThreshold = 3;

QFont editorFont(const AppearanceSettings &appearance)
{
    QFont font = appearance.fontFamily.isEmpty()
                     ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                     : QFont(appearance.fontFamily);
    font.setPointSize(appearance.fontSize);
    font.setFixedPitch(true);
    return font;
}

// Keywords and common library names offered alongside words from the document.
const char *const completionWords =
    "auto bool break case catch char class const constexpr continue default delete double else "
    "enum explicit extern false float for friend if include inline int long namespace new "
    "noexcept nullptr operator override private protected public return short signed sizeof "
    "static struct switch template this throw true try typedef typename union unsigned using "
    "virtual void volatile while "
    "cin cout cerr endl getline printf scanf puts fgets malloc free strlen strcmp memset memcpy "
    "string vector map set unordered_map unordered_set pair queue stack deque priority_queue "
    "sort reverse min max swap accumulate lower_bound upper_bound make_pair push_back pop_back "
    "emplace_back begin end size empty";

} // namespace

CodeEditor::CodeEditor(QWidget *parent)
    : QsciScintilla(parent)
    , m_lexer(new CppLexer(this))
{
    setUtf8(true);
    setFrameShape(QFrame::NoFrame);
    setLexer(m_lexer);

    auto *apis = new QsciAPIs(m_lexer);
    const QStringList words = QString::fromLatin1(completionWords).split(QLatin1Char(' '));
    for (const QString &word : words)
        apis->add(word);
    apis->prepare();

    setMarginType(LineNumberColumn, NumberMargin);
    setMarginType(SymbolColumn, SymbolMargin);
    setMarginWidth(SymbolColumn, 18);
    setMarginSensitivity(SymbolColumn, true);
    setMarginSensitivity(LineNumberColumn, true);
    setMarginMarkerMask(LineNumberColumn, 0);
    setMarginMarkerMask(SymbolColumn, (1 << BreakpointMarker) | (1 << ExecutionArrowMarker)
                                          | (1 << ErrorMarker) | (1 << WarningMarker));

    markerDefine(Circle, BreakpointMarker);
    markerDefine(RightArrow, ExecutionArrowMarker);
    markerDefine(Background, ExecutionLineMarker);
    markerDefine(LeftRectangle, ErrorMarker);
    markerDefine(LeftRectangle, WarningMarker);

    indicatorDefine(SquiggleIndicator, ErrorIndicator);
    indicatorDefine(SquiggleIndicator, WarningIndicator);

    setBackspaceUnindents(true);
    setTabIndents(true);
    setIndentationGuides(true);
    setCaretLineVisible(true);
    setCaretWidth(2);
    setScrollWidthTracking(true);
    setScrollWidth(1);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAutoCompletionCaseSensitivity(true);
    setAutoCompletionShowSingle(false);
    setAutoCompletionFillupsEnabled(false);
    setAutoCompletionThreshold(completionThreshold);

    // Greying out inactive #if branches needs a preprocessor model QScintilla's
    // defaults cannot configure well, so every branch is styled as active.
    SendScintilla(SCI_SETPROPERTY, "lexer.cpp.track.preprocessor", "0");

    SendScintilla(SCI_SETMOUSEDWELLTIME, dwellTimeMs);

    connect(this, &QsciScintilla::marginClicked, this, &CodeEditor::onMarginClicked);
    connect(this, &QsciScintilla::linesChanged, this, &CodeEditor::updateLineNumberMargin);
    connect(this, &QsciScintillaBase::SCN_CHARADDED, this, &CodeEditor::onCharAdded);
    connect(this, &QsciScintillaBase::SCN_DWELLSTART, this, &CodeEditor::showDiagnosticTip);
    connect(this, &QsciScintillaBase::SCN_DWELLEND, this, [] { QToolTip::hideText(); });
}

QString CodeEditor::displayName() const
{
    return m_filePath.isEmpty() ? m_untitledName : QFileInfo(m_filePath).fileName();
}

void CodeEditor::applySettings(const Settings &settings)
{
    const EditorSettings &editor = settings.editor;
    const AppearanceSettings &appearance = settings.appearance;
    const ThemeColors &theme = Theme::colors(appearance.theme);
    const QFont font = editorFont(appearance);
    const QColor background = appearance.editorBackground.isEmpty()
                                  ? theme.editorBackground
                                  : QColor(appearance.editorBackground);
    const QColor foreground = SyntaxHighlighter::color(appearance, QStringLiteral("default"));

    SyntaxHighlighter::apply(m_lexer, appearance, font);
    // setLexer() resets some global styles, so everything below follows it.
    setLexer(m_lexer);

    setPaper(background);
    setColor(foreground);
    setCaretForegroundColor(foreground);
    setCaretLineBackgroundColor(theme.currentLine);
    setSelectionBackgroundColor(theme.selection);
    setSelectionForegroundColor(foreground);
    setIndentationGuidesForegroundColor(theme.border);
    setIndentationGuidesBackgroundColor(background);

    setMarginsFont(font);
    setMarginsBackgroundColor(background);
    setMarginsForegroundColor(theme.gutterText);
    setFoldMarginColors(background, background);

    setMarkerBackgroundColor(theme.error, BreakpointMarker);
    setMarkerForegroundColor(theme.error, BreakpointMarker);
    setMarkerBackgroundColor(theme.warning, ExecutionArrowMarker);
    setMarkerForegroundColor(theme.warning, ExecutionArrowMarker);
    setMarkerBackgroundColor(theme.executionLine, ExecutionLineMarker);
    setMarkerBackgroundColor(theme.error, ErrorMarker);
    setMarkerForegroundColor(theme.error, ErrorMarker);
    setMarkerBackgroundColor(theme.warning, WarningMarker);
    setMarkerForegroundColor(theme.warning, WarningMarker);
    setIndicatorForegroundColor(theme.error, ErrorIndicator);
    setIndicatorForegroundColor(theme.warning, WarningIndicator);

    setMatchedBraceBackgroundColor(theme.selection);
    setMatchedBraceForegroundColor(foreground);
    setUnmatchedBraceBackgroundColor(background);
    setUnmatchedBraceForegroundColor(theme.error);

    setTabWidth(editor.tabWidth);
    setIndentationWidth(0);   // Follow the tab width.
    setIndentationsUseTabs(!editor.insertSpaces);
    setAutoIndent(editor.autoIndent);
    setBraceMatching(editor.bracketMatching ? SloppyBraceMatch : NoBraceMatch);
    setWrapMode(editor.wordWrap ? WrapWord : WrapNone);
    setFolding(editor.codeFolding ? PlainFoldStyle : NoFoldStyle, FoldColumn);
    styleFoldMarkers(theme.gutterText);
    setAutoCompletionSource(editor.autoComplete ? AcsAll : AcsNone);
    m_autoCloseBrackets = editor.autoCloseBrackets;

    m_showLineNumbers = editor.lineNumbers;
    m_lineNumberDigits = 0;
    setMarginLineNumbers(LineNumberColumn, m_showLineNumbers);
    updateLineNumberMargin();
}

// Plain triangles in the gutter colour, in place of QScintilla's white boxes.
void CodeEditor::styleFoldMarkers(const QColor &color)
{
    SendScintilla(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, static_cast<long>(SC_MARK_ARROW));
    SendScintilla(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, static_cast<long>(SC_MARK_ARROWDOWN));
    for (const int marker : {SC_MARKNUM_FOLDEREND, SC_MARKNUM_FOLDEROPENMID, SC_MARKNUM_FOLDERMIDTAIL,
                             SC_MARKNUM_FOLDERTAIL, SC_MARKNUM_FOLDERSUB}) {
        SendScintilla(SCI_MARKERDEFINE, marker, static_cast<long>(SC_MARK_EMPTY));
    }
    for (const int marker : {SC_MARKNUM_FOLDER, SC_MARKNUM_FOLDEROPEN}) {
        SendScintilla(SCI_MARKERSETFORE, marker, color);
        SendScintilla(SCI_MARKERSETBACK, marker, color);
    }
}

void CodeEditor::loadText(const QString &text, EndOfLine eol)
{
    setText(text);
    setLineEnding(eol);
    SendScintilla(SCI_EMPTYUNDOBUFFER);
    setModified(false);
    updateLineNumberMargin();
}

EndOfLine CodeEditor::lineEnding() const
{
    switch (eolMode()) {
    case EolWindows: return EndOfLine::Windows;
    case EolMac: return EndOfLine::ClassicMac;
    case EolUnix: break;
    }
    return EndOfLine::Unix;
}

void CodeEditor::setLineEnding(EndOfLine eol)
{
    switch (eol) {
    case EndOfLine::Unix: setEolMode(EolUnix); break;
    case EndOfLine::Windows: setEolMode(EolWindows); break;
    case EndOfLine::ClassicMac: setEolMode(EolMac); break;
    }
}

void CodeEditor::updateLineNumberMargin()
{
    if (!m_showLineNumbers) {
        setMarginWidth(LineNumberColumn, 0);
        return;
    }
    const int digits = std::max(3, static_cast<int>(QString::number(lines()).size()));
    if (digits == m_lineNumberDigits)
        return;
    m_lineNumberDigits = digits;
    setMarginWidth(LineNumberColumn, QString(digits + 1, QLatin1Char('0')));
}

void CodeEditor::onMarginClicked(int margin, int line, Qt::KeyboardModifiers)
{
    if (margin == SymbolColumn || margin == LineNumberColumn)
        toggleBreakpoint(line + 1);
}

void CodeEditor::toggleBreakpoint(int line)
{
    const bool enabled = !(markersAtLine(line - 1) & (1U << BreakpointMarker));
    setBreakpoint(line, enabled);
    emit breakpointToggled(line, enabled);
}

void CodeEditor::setBreakpoint(int line, bool enabled)
{
    if (enabled)
        markerAdd(line - 1, BreakpointMarker);
    else
        markerDelete(line - 1, BreakpointMarker);
}

QList<int> CodeEditor::breakpointLines() const
{
    QList<int> result;
    int line = -1;
    while ((line = markerFindNext(line + 1, 1U << BreakpointMarker)) >= 0)
        result.append(line + 1);
    return result;
}

void CodeEditor::setExecutionLine(int line)
{
    markerDeleteAll(ExecutionArrowMarker);
    markerDeleteAll(ExecutionLineMarker);
    m_executionLine = line;
    // The caret sits on this line while paused, and would paint over its highlight.
    setCaretLineVisible(line <= 0);
    if (line <= 0)
        return;
    markerAdd(line - 1, ExecutionArrowMarker);
    markerAdd(line - 1, ExecutionLineMarker);
    goToPosition(line);
}

void CodeEditor::clearDiagnostics()
{
    m_lineMessages.clear();
    markerDeleteAll(ErrorMarker);
    markerDeleteAll(WarningMarker);
    const int lastLine = std::max(0, lines() - 1);
    const int lastIndex = lineLength(lastLine);
    clearIndicatorRange(0, 0, lastLine, lastIndex, ErrorIndicator);
    clearIndicatorRange(0, 0, lastLine, lastIndex, WarningIndicator);
}

void CodeEditor::setDiagnostics(const QList<Diagnostic> &diagnostics)
{
    clearDiagnostics();

    for (const Diagnostic &diagnostic : diagnostics) {
        if (diagnostic.line <= 0 || diagnostic.line > lines()
            || diagnostic.severity == Diagnostic::Severity::Note) {
            continue;
        }
        const int line = diagnostic.line - 1;
        const bool isError = diagnostic.severity == Diagnostic::Severity::Error;

        QString &message = m_lineMessages[line];
        if (!message.isEmpty())
            message += QLatin1Char('\n');
        message += diagnostic.message;

        markerAdd(line, isError ? ErrorMarker : WarningMarker);

        const long lineStart = SendScintilla(SCI_POSITIONFROMLINE, line);
        const long lineEnd = SendScintilla(SCI_GETLINEENDPOSITION, line);
        long start = lineStart + std::max(0, diagnostic.column - 1);
        start = std::min(start, lineEnd);
        long end = SendScintilla(SCI_WORDENDPOSITION, start, 1L);
        if (end <= start)
            end = std::min(start + 1, lineEnd);
        if (diagnostic.column <= 0) {
            start = SendScintilla(SCI_GETLINEINDENTPOSITION, line);
            end = lineEnd;
        }

        SendScintilla(SCI_SETINDICATORCURRENT, isError ? ErrorIndicator : WarningIndicator);
        SendScintilla(SCI_INDICATORFILLRANGE, start, end - start);
    }
}

void CodeEditor::showDiagnosticTip(int position, int x, int y)
{
    if (position < 0)
        return;
    const int line = static_cast<int>(SendScintilla(SCI_LINEFROMPOSITION, position));
    const auto message = m_lineMessages.constFind(line);
    if (message != m_lineMessages.constEnd())
        QToolTip::showText(viewport()->mapToGlobal(QPoint(x, y)), *message, this);
}

void CodeEditor::goToPosition(int line, int column)
{
    const int target = std::clamp(line - 1, 0, std::max(0, lines() - 1));
    const long lineStart = SendScintilla(SCI_POSITIONFROMLINE, target);
    const long lineEnd = SendScintilla(SCI_GETLINEENDPOSITION, target);
    const long position = column > 0 ? std::min(lineStart + column - 1, lineEnd)
                                     : static_cast<long>(SendScintilla(SCI_GETLINEINDENTPOSITION, target));

    SendScintilla(SCI_ENSUREVISIBLEENFORCEPOLICY, target);
    SendScintilla(SCI_GOTOPOS, position);
    SendScintilla(SCI_SCROLLCARET);
    setFocus();
}

void CodeEditor::toggleLineComment()
{
    int lineFrom, indexFrom, lineTo, indexTo;
    getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
    if (lineFrom < 0) {
        getCursorPosition(&lineFrom, &indexFrom);
        lineTo = lineFrom;
    } else if (indexTo == 0 && lineTo > lineFrom) {
        --lineTo;   // A selection ending at column 0 does not include that line.
    }

    bool allCommented = true;
    for (int line = lineFrom; line <= lineTo; ++line) {
        const QString trimmed = text(line).trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1String("//")))
            allCommented = false;
    }

    beginUndoAction();
    for (int line = lineFrom; line <= lineTo; ++line) {
        const QString content = text(line);
        if (content.trimmed().isEmpty())
            continue;
        const int indent = static_cast<int>(std::find_if(content.cbegin(), content.cend(),
                                                         [](QChar c) { return !c.isSpace(); })
                                            - content.cbegin());
        if (allCommented) {
            const int length = content.mid(indent + 2, 1) == QLatin1String(" ") ? 3 : 2;
            setSelection(line, indent, line, indent + length);
            removeSelectedText();
        } else {
            insertAt(QStringLiteral("// "), line, indent);
        }
    }
    endUndoAction();
}

QString CodeEditor::wordAtCursor() const
{
    int line, index;
    getCursorPosition(&line, &index);
    return wordAtLineIndex(line, index);
}

int CodeEditor::charAt(long position) const
{
    return static_cast<int>(SendScintilla(SCI_GETCHARAT, position));
}

void CodeEditor::onCharAdded(int character)
{
    if (m_autoCloseBrackets)
        autoCloseBracket(character);
}

void CodeEditor::autoCloseBracket(int character)
{
    const long caret = SendScintilla(SCI_GETCURRENTPOS);
    const int next = charAt(caret);

    // Enter between { and } gives the closing brace a line of its own.
    if (character == '\n' || character == '\r') {
        if (next != '}')
            return;
        const int line = static_cast<int>(SendScintilla(SCI_LINEFROMPOSITION, caret));
        if (line == 0)
            return;
        const QString above = text(line - 1).trimmed();
        if (!above.endsWith(QLatin1Char('{')))
            return;

        int indent = indentation(line);
        const int width = tabWidth();
        beginUndoAction();
        SendScintilla(SCI_NEWLINE);
        if (autoIndent()) {
            setIndentation(line, indentation(line - 1) + width);
            indent = indentation(line - 1);
            setIndentation(line + 1, indent);
        }
        endUndoAction();
        SendScintilla(SCI_GOTOPOS, SendScintilla(SCI_GETLINEENDPOSITION, line));
        return;
    }

    // Typing a closer that is already there steps over it.
    if ((character == ')' || character == ']' || character == '}' || character == '"'
         || character == '\'')
        && next == character) {
        SendScintilla(SCI_SETSEL, caret, caret + 1);
        SendScintilla(SCI_REPLACESEL, "");
        return;
    }

    char closer = 0;
    switch (character) {
    case '(': closer = ')'; break;
    case '[': closer = ']'; break;
    case '{': closer = '}'; break;
    case '"': closer = '"'; break;
    case '\'': closer = '\''; break;
    default: return;
    }

    // No pairing right before a word, as in wrapping an existing expression.
    if (next != 0 && next != ' ' && next != '\t' && next != '\r' && next != '\n' && next != ')'
        && next != ']' && next != '}' && next != ';' && next != ',') {
        return;
    }

    if (closer == '"' || closer == '\'') {
        const int style = static_cast<int>(SendScintilla(SCI_GETSTYLEAT, caret - 2));
        const auto isTextStyle = [](int s) {
            return s == QsciLexerCPP::Comment || s == QsciLexerCPP::CommentLine
                   || s == QsciLexerCPP::CommentDoc || s == QsciLexerCPP::DoubleQuotedString
                   || s == QsciLexerCPP::SingleQuotedString;
        };
        if (caret >= 2 && isTextStyle(style))
            return;
        // An apostrophe after a letter is prose or a digit separator.
        if (closer == '\'' && caret >= 2 && QChar::isLetterOrNumber(charAt(caret - 2)))
            return;
    }

    const char insertion[2] = {closer, '\0'};
    SendScintilla(SCI_INSERTTEXT, caret, insertion);
}

void CodeEditor::dragEnterEvent(QDragEnterEvent *event)
{
    // Dropped files go to the main window to be opened, not pasted as text.
    if (event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    QsciScintilla::dragEnterEvent(event);
}
