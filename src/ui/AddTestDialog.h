#pragma once

#include "leetcode/ProblemFolder.h"

#include <QDialog>
#include <QList>

class QLabel;
class QLineEdit;

// Asks for each input of a custom test, checking values against the C++ types
// so that a typo is caught here and not as a compile error in the harness.
class AddTestDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddTestDialog(const ProblemMetadata &metadata, QWidget *parent = nullptr);

    // The test as entered, once accepted.
    LcTestCase test() const { return m_test; }

    QList<QLineEdit *> inputEdits() const { return m_inputs; }
    QLineEdit *expectedEdit() const { return m_expected; }
    QString errorText() const;
    void tryAccept();

private:
    ProblemMetadata m_metadata;
    QStringList m_keys;
    QList<CppType> m_types;   // Empty when types can't be known, as with design problems.
    QList<QLineEdit *> m_inputs;
    QLineEdit *m_expected;
    QLabel *m_error;
    LcTestCase m_test;
};
