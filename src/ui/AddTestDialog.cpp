#include "ui/AddTestDialog.h"

#include "leetcode/QuestionParser.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

AddTestDialog::AddTestDialog(const ProblemMetadata &metadata, QWidget *parent)
    : QDialog(parent)
    , m_metadata(metadata)
    , m_keys(QuestionParser::inputKeys(metadata.meta))
    , m_expected(new QLineEdit)
    , m_error(new QLabel)
{
    setWindowTitle(tr("Add Custom Test"));
    resize(520, 0);

    const HarnessPlan plan = HarnessGenerator::planHarness(
        metadata.meta, CppSignature::analyzeStarterCode(metadata.starterCode));
    if (plan.kind == HarnessPlan::Kind::Function) {
        for (const CppParam &param : plan.method.params)
            m_types.append(param.type);
    }

    auto *form = new QFormLayout;
    for (qsizetype i = 0; i < m_keys.size(); ++i) {
        auto *edit = new QLineEdit;
        if (i < m_types.size())
            edit->setPlaceholderText(CppSignature::formatType(m_types.at(i), true));
        m_inputs.append(edit);
        form->addRow(m_keys.at(i) + QLatin1Char(':'), edit);
    }
    m_expected->setPlaceholderText(tr("Optional. Without it the test only shows its output"));
    form->addRow(tr("Expected:"), m_expected);

    m_error->setWordWrap(true);
    m_error->hide();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &AddTestDialog::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_error);
    layout->addWidget(buttons);
}

QString AddTestDialog::errorText() const
{
    return m_error->text();
}

void AddTestDialog::tryAccept()
{
    LcTestCase test;
    test.source = TestSource::Custom;
    try {
        for (qsizetype i = 0; i < m_inputs.size(); ++i) {
            const LcValue value = LcValueParser::parse(m_inputs.at(i)->text());
            if (i < m_types.size())
                HarnessGenerator::literal(m_types.at(i), value);
            test.inputs.append(value);
        }
        if (!m_expected->text().trimmed().isEmpty())
            test.expected = QuestionParser::parseOutputText(m_expected->text());
    } catch (const LcError &error) {
        m_error->setText(error.message());
        m_error->show();
        return;
    }
    m_test = test;
    accept();
}
