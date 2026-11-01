#pragma once

#include <QWidget>

class CodeEditor;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

// Find and replace, docked under the editor so that it never covers code.
class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);

    void setEditor(CodeEditor *editor);
    void showFind();
    void showReplace();

    bool findNext();
    bool findPrevious();
    void replaceCurrent();
    int replaceAll();

    void setSearchText(const QString &text);
    void setReplaceText(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool find(bool forward, bool fromSelectionStart);
    void reveal(bool withReplace);
    void onSearchTextChanged();

    CodeEditor *m_editor = nullptr;
    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QWidget *m_replaceRow;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_wholeWord;
    QCheckBox *m_regex;
    QLabel *m_status;
};
