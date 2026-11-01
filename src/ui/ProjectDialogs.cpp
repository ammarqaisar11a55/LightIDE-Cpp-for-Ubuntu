#include "ui/ProjectDialogs.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace {

QWidget *withBrowseButton(QLineEdit *edit, QWidget *dialog)
{
    auto *browse = new QPushButton(QObject::tr("Browse..."));
    QObject::connect(browse, &QPushButton::clicked, dialog, [edit, dialog] {
        const QString path =
            QFileDialog::getExistingDirectory(dialog, QObject::tr("Choose Directory"), edit->text());
        if (!path.isEmpty())
            edit->setText(path);
    });

    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    return row;
}

QDialogButtonBox *okCancelButtons(QDialog *dialog)
{
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

QComboBox *standardCombo(const QStringList &values, const QString &current)
{
    auto *combo = new QComboBox;
    combo->addItems(values);
    combo->setCurrentText(current);
    return combo;
}

QComboBox *optimizationCombo(const QString &current)
{
    return standardCombo({QStringLiteral("-O0"), QStringLiteral("-O1"), QStringLiteral("-O2"),
                          QStringLiteral("-O3"), QStringLiteral("-Os"), QStringLiteral("-Og")},
                         current);
}

QStringList splitSemicolons(const QString &text)
{
    QStringList parts;
    const QStringList raw = text.split(QLatin1Char(';'));
    for (const QString &part : raw) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            parts << trimmed;
    }
    return parts;
}

} // namespace

RunOptionsDialog::RunOptionsDialog(const FileRunOptions &options, QWidget *parent)
    : QDialog(parent)
    , m_arguments(new QLineEdit(options.arguments))
    , m_workingDirectory(new QLineEdit(options.workingDirectory))
    , m_outputName(new QLineEdit(options.outputName))
{
    setWindowTitle(tr("Run Options"));
    resize(520, 0);

    m_arguments->setPlaceholderText(tr("Command-line arguments, quoted as in a shell"));
    m_workingDirectory->setPlaceholderText(tr("The executable's directory"));
    m_outputName->setPlaceholderText(tr("The source file's name"));

    auto *form = new QFormLayout;
    form->addRow(tr("Arguments:"), m_arguments);
    form->addRow(tr("Working directory:"), withBrowseButton(m_workingDirectory, this));
    form->addRow(tr("Output executable name:"), m_outputName);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(okCancelButtons(this));
}

FileRunOptions RunOptionsDialog::options() const
{
    return FileRunOptions{m_arguments->text(), m_workingDirectory->text().trimmed(),
                          m_outputName->text().trimmed()};
}

NewProjectDialog::NewProjectDialog(const QString &defaultLocation, QWidget *parent)
    : QDialog(parent)
    , m_name(new QLineEdit)
    , m_location(new QLineEdit(defaultLocation))
    , m_language(new QComboBox)
{
    setWindowTitle(tr("New Project"));
    resize(520, 0);

    m_name->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(R"([\w][\w .+-]*)")), m_name));
    m_language->addItem(tr("C++"));
    m_language->addItem(tr("C"));

    auto *form = new QFormLayout;
    form->addRow(tr("Project name:"), m_name);
    form->addRow(tr("Location:"), withBrowseButton(m_location, this));
    form->addRow(tr("Language:"), m_language);

    auto *buttons = okCancelButtons(this);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setEnabled(false);
    connect(m_name, &QLineEdit::textChanged, ok,
            [ok](const QString &text) { ok->setEnabled(!text.trimmed().isEmpty()); });

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString NewProjectDialog::projectName() const
{
    return m_name->text().trimmed();
}

QString NewProjectDialog::projectDirectory() const
{
    return QDir(m_location->text().trimmed()).filePath(projectName());
}

bool NewProjectDialog::isCpp() const
{
    return m_language->currentIndex() == 0;
}

ProjectOptionsDialog::ProjectOptionsDialog(const Project &project, QWidget *parent)
    : QDialog(parent)
    , m_target(new QLineEdit(project.targetName))
    , m_targetType(new QComboBox)
    , m_cStandard(standardCombo({QStringLiteral("c89"), QStringLiteral("c99"), QStringLiteral("c11"),
                                 QStringLiteral("c17"), QStringLiteral("c2x")},
                                project.cStandard))
    , m_cxxStandard(standardCombo({QStringLiteral("c++11"), QStringLiteral("c++14"),
                                   QStringLiteral("c++17"), QStringLiteral("c++20"),
                                   QStringLiteral("c++23")},
                                  project.cxxStandard))
    , m_debugOptimization(optimizationCombo(project.debugOptimization))
    , m_releaseOptimization(optimizationCombo(project.releaseOptimization))
    , m_warningFlags(new QLineEdit(project.warningFlags.join(QLatin1Char(' '))))
    , m_defines(new QLineEdit(project.defines.join(QStringLiteral("; "))))
    , m_includeDirs(new QLineEdit(project.includeDirs.join(QStringLiteral("; "))))
    , m_libraryDirs(new QLineEdit(project.libraryDirs.join(QStringLiteral("; "))))
    , m_libraries(new QLineEdit(project.libraries.join(QStringLiteral("; "))))
    , m_extraArgs(new QLineEdit(project.extraArgs))
    , m_runArguments(new QLineEdit(project.runArguments))
    , m_workingDirectory(new QLineEdit(project.workingDirectory))
{
    setWindowTitle(tr("Project Options"));
    resize(560, 0);

    m_targetType->addItem(tr("Executable"));
    m_targetType->addItem(tr("Static library"));
    m_targetType->addItem(tr("Shared library"));
    m_targetType->setCurrentIndex(static_cast<int>(project.targetType));

    m_defines->setPlaceholderText(tr("Separated by semicolons, such as: DEBUG; VERSION=2"));
    m_includeDirs->setPlaceholderText(tr("Relative to the project, separated by semicolons"));
    m_libraryDirs->setPlaceholderText(tr("Relative to the project, separated by semicolons"));
    m_libraries->setPlaceholderText(tr("Names without -l, separated by semicolons"));
    m_workingDirectory->setPlaceholderText(tr("The project directory"));

    auto *form = new QFormLayout;
    form->addRow(tr("Target name:"), m_target);
    form->addRow(tr("Target type:"), m_targetType);
    form->addRow(tr("C standard:"), m_cStandard);
    form->addRow(tr("C++ standard:"), m_cxxStandard);
    form->addRow(tr("Debug optimization:"), m_debugOptimization);
    form->addRow(tr("Release optimization:"), m_releaseOptimization);
    form->addRow(tr("Warning flags:"), m_warningFlags);
    form->addRow(tr("Defines:"), m_defines);
    form->addRow(tr("Include directories:"), m_includeDirs);
    form->addRow(tr("Library directories:"), m_libraryDirs);
    form->addRow(tr("Libraries:"), m_libraries);
    form->addRow(tr("Additional arguments:"), m_extraArgs);
    form->addRow(tr("Run arguments:"), m_runArguments);
    form->addRow(tr("Working directory:"), withBrowseButton(m_workingDirectory, this));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(okCancelButtons(this));
}

void ProjectOptionsDialog::applyTo(Project &project) const
{
    project.targetName = m_target->text().trimmed();
    project.targetType = static_cast<TargetType>(m_targetType->currentIndex());
    project.cStandard = m_cStandard->currentText();
    project.cxxStandard = m_cxxStandard->currentText();
    project.debugOptimization = m_debugOptimization->currentText();
    project.releaseOptimization = m_releaseOptimization->currentText();
    project.warningFlags = QProcess::splitCommand(m_warningFlags->text());
    project.defines = splitSemicolons(m_defines->text());
    project.includeDirs = splitSemicolons(m_includeDirs->text());
    project.libraryDirs = splitSemicolons(m_libraryDirs->text());
    project.libraries = splitSemicolons(m_libraries->text());
    project.extraArgs = m_extraArgs->text().trimmed();
    project.runArguments = m_runArguments->text();
    project.workingDirectory = m_workingDirectory->text().trimmed();
}
