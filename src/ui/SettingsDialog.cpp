#include "ui/SettingsDialog.h"

#include "editor/SyntaxHighlighter.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

// A colour swatch that also knows how to fall back to "use the theme's colour".
class ColorButton : public QPushButton
{
public:
    explicit ColorButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFixedWidth(120);
        connect(this, &QPushButton::clicked, this, [this] {
            const QColor chosen = QColorDialog::getColor(
                m_color.isValid() ? m_color : QColor(Qt::white), this);
            if (chosen.isValid())
                setColor(chosen);
        });
        refresh();
    }

    QColor color() const { return m_color; }

    void setColor(const QColor &color)
    {
        m_color = color;
        refresh();
    }

private:
    void refresh()
    {
        setText(m_color.isValid() ? m_color.name() : tr("Theme default"));
        if (!m_color.isValid()) {
            setStyleSheet({});
            return;
        }
        const QString foreground = m_color.lightness() > 128 ? QStringLiteral("#000000")
                                                             : QStringLiteral("#ffffff");
        setStyleSheet(QStringLiteral("background: %1; color: %2;").arg(m_color.name(), foreground));
    }

    QColor m_color;
};

namespace {

QComboBox *optimizationCombo()
{
    auto *combo = new QComboBox;
    combo->addItems({QStringLiteral("-O0"), QStringLiteral("-O1"), QStringLiteral("-O2"),
                     QStringLiteral("-O3"), QStringLiteral("-Os"), QStringLiteral("-Og")});
    return combo;
}

QStringList nonEmptyLines(const QString &text)
{
    QStringList lines;
    const QStringList parts = text.split(QLatin1Char('\n'));
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            lines << trimmed;
    }
    return lines;
}

void selectValue(QComboBox *combo, const QString &value)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : combo->findText(value));
}

} // namespace

SettingsDialog::SettingsDialog(Settings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_tabs(new QTabWidget)
{
    setWindowTitle(tr("Settings"));
    resize(640, 600);

    m_tabs->addTab(createAppearancePage(), tr("Appearance"));
    m_tabs->addTab(createEditorPage(), tr("Editor"));
    m_tabs->addTab(createCompilerPage(), tr("Compiler"));
    m_tabs->addTab(createLeetCodePage(), tr("LeetCode"));

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        applyToSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &SettingsDialog::applyToSettings);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttons);

    loadFromSettings();
}

void SettingsDialog::showPage(Page page)
{
    m_tabs->setCurrentIndex(page);
}

QWidget *SettingsDialog::createAppearancePage()
{
    m_theme = new QComboBox;
    m_theme->addItem(tr("Dark"), QStringLiteral("dark"));
    m_theme->addItem(tr("Light"), QStringLiteral("light"));

    m_fontFamily = new QFontComboBox;
    m_fontFamily->setFontFilters(QFontComboBox::MonospacedFonts);
    m_fontSize = new QSpinBox;
    m_fontSize->setRange(6, 48);
    m_background = new ColorButton;
    auto *resetBackground = new QPushButton(tr("Reset"));
    connect(resetBackground, &QPushButton::clicked, this, [this] { m_background->setColor({}); });

    auto *backgroundRow = new QHBoxLayout;
    backgroundRow->addWidget(m_background);
    backgroundRow->addWidget(resetBackground);
    backgroundRow->addStretch();

    m_sidebarVisible = new QCheckBox(tr("Show the sidebar"));
    m_panelVisible = new QCheckBox(tr("Show the bottom panel"));

    auto *form = new QFormLayout;
    form->addRow(tr("Theme:"), m_theme);
    form->addRow(tr("Editor font:"), m_fontFamily);
    form->addRow(tr("Font size:"), m_fontSize);
    form->addRow(tr("Editor background:"), backgroundRow);
    form->addRow(QString(), m_sidebarVisible);
    form->addRow(QString(), m_panelVisible);

    auto *syntaxGroup = new QGroupBox(tr("Syntax highlighting colors"));
    auto *grid = new QGridLayout(syntaxGroup);
    const QList<SyntaxHighlighter::Token> tokens = SyntaxHighlighter::tokens();
    int row = 0;
    for (const SyntaxHighlighter::Token &token : tokens) {
        auto *button = new ColorButton;
        auto *reset = new QPushButton(tr("Reset"));
        connect(reset, &QPushButton::clicked, this, [button] { button->setColor({}); });
        m_syntaxButtons.insert(token.key, button);
        grid->addWidget(new QLabel(token.label), row, 0);
        grid->addWidget(button, row, 1);
        grid->addWidget(reset, row, 2);
        ++row;
    }
    grid->setColumnStretch(0, 1);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addLayout(form);
    layout->addWidget(syntaxGroup);
    layout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

QWidget *SettingsDialog::createEditorPage()
{
    m_tabWidth = new QSpinBox;
    m_tabWidth->setRange(1, 16);
    m_insertSpaces = new QCheckBox(tr("Insert spaces instead of tabs"));
    m_autoIndent = new QCheckBox(tr("Automatic and smart indentation"));
    m_autoClose = new QCheckBox(tr("Close brackets and quotes automatically"));
    m_autoComplete = new QCheckBox(tr("Suggest completions while typing"));
    m_folding = new QCheckBox(tr("Code folding"));
    m_lineNumbers = new QCheckBox(tr("Line numbers"));
    m_wordWrap = new QCheckBox(tr("Word wrap"));
    m_bracketMatching = new QCheckBox(tr("Bracket matching"));

    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->addRow(tr("Tab width:"), m_tabWidth);
    for (QCheckBox *box : {m_insertSpaces, m_autoIndent, m_autoClose, m_autoComplete, m_folding,
                           m_lineNumbers, m_wordWrap, m_bracketMatching}) {
        form->addRow(QString(), box);
    }
    return page;
}

QWidget *SettingsDialog::createCompilerPage()
{
    m_gccPath = new QLineEdit;
    m_gxxPath = new QLineEdit;
    m_gdbPath = new QLineEdit;

    m_cStandard = new QComboBox;
    m_cStandard->addItem(QStringLiteral("C89"), QStringLiteral("c89"));
    m_cStandard->addItem(QStringLiteral("C99"), QStringLiteral("c99"));
    m_cStandard->addItem(QStringLiteral("C11"), QStringLiteral("c11"));
    m_cStandard->addItem(QStringLiteral("C17"), QStringLiteral("c17"));
    m_cStandard->addItem(QStringLiteral("C23"), QStringLiteral("c2x"));

    m_cxxStandard = new QComboBox;
    m_cxxStandard->addItem(QStringLiteral("C++11"), QStringLiteral("c++11"));
    m_cxxStandard->addItem(QStringLiteral("C++14"), QStringLiteral("c++14"));
    m_cxxStandard->addItem(QStringLiteral("C++17"), QStringLiteral("c++17"));
    m_cxxStandard->addItem(QStringLiteral("C++20"), QStringLiteral("c++20"));
    m_cxxStandard->addItem(QStringLiteral("C++23"), QStringLiteral("c++23"));

    m_debugOptimization = optimizationCombo();
    m_releaseOptimization = optimizationCombo();
    m_warningFlags = new QLineEdit;
    m_includeDirs = new QPlainTextEdit;
    m_libraryDirs = new QPlainTextEdit;
    m_includeDirs->setPlaceholderText(tr("One directory per line"));
    m_libraryDirs->setPlaceholderText(tr("One directory per line"));
    m_includeDirs->setMaximumHeight(64);
    m_libraryDirs->setMaximumHeight(64);
    m_libraries = new QLineEdit;
    m_libraries->setPlaceholderText(tr("Names without -l, such as: m pthread"));
    m_additionalArgs = new QLineEdit;
    m_outputDirectory = new QLineEdit;
    m_outputDirectory->setPlaceholderText(tr("Next to the source file"));

    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->addRow(tr("GCC path:"), m_gccPath);
    form->addRow(tr("G++ path:"), m_gxxPath);
    form->addRow(tr("GDB path:"), m_gdbPath);
    form->addRow(tr("Default C standard:"), m_cStandard);
    form->addRow(tr("Default C++ standard:"), m_cxxStandard);
    form->addRow(tr("Debug optimization:"), m_debugOptimization);
    form->addRow(tr("Release optimization:"), m_releaseOptimization);
    form->addRow(tr("Warning flags:"), m_warningFlags);
    form->addRow(tr("Include directories:"), m_includeDirs);
    form->addRow(tr("Library directories:"), m_libraryDirs);
    form->addRow(tr("Libraries:"), m_libraries);
    form->addRow(tr("Additional arguments:"), m_additionalArgs);
    form->addRow(tr("Default output directory:"), m_outputDirectory);
    return page;
}

QWidget *SettingsDialog::createLeetCodePage()
{
    m_lcDirectory = new QLineEdit;
    m_lcDirectory->setPlaceholderText(QDir::home().filePath(QStringLiteral("leetcode")));
    m_lcStandard = new QComboBox;
    m_lcStandard->addItems({QStringLiteral("c++17"), QStringLiteral("c++20"), QStringLiteral("c++23")});
    m_lcFlags = new QLineEdit;
    m_lcTimeout = new QSpinBox;
    m_lcTimeout->setRange(100, 120000);
    m_lcTimeout->setSingleStep(500);
    m_lcTimeout->setSuffix(tr(" ms"));
    m_lcSanitizers = new QCheckBox(tr("Build with AddressSanitizer and UBSan, like LeetCode's judge"));

    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->addRow(tr("Problems folder:"), m_lcDirectory);
    form->addRow(tr("C++ standard:"), m_lcStandard);
    form->addRow(tr("Compiler flags:"), m_lcFlags);
    form->addRow(tr("Time limit per test:"), m_lcTimeout);
    form->addRow(QString(), m_lcSanitizers);
    return page;
}

void SettingsDialog::loadFromSettings()
{
    const AppearanceSettings &appearance = m_settings->appearance;
    selectValue(m_theme, appearance.theme == ThemeKind::Light ? QStringLiteral("light")
                                                              : QStringLiteral("dark"));
    if (!appearance.fontFamily.isEmpty())
        m_fontFamily->setCurrentFont(QFont(appearance.fontFamily));
    m_fontSize->setValue(appearance.fontSize);
    m_background->setColor(QColor(appearance.editorBackground));
    for (auto it = m_syntaxButtons.begin(); it != m_syntaxButtons.end(); ++it)
        it.value()->setColor(QColor(appearance.syntaxColors.value(it.key())));
    m_sidebarVisible->setChecked(appearance.sidebarVisible);
    m_panelVisible->setChecked(appearance.bottomPanelVisible);

    const EditorSettings &editor = m_settings->editor;
    m_tabWidth->setValue(editor.tabWidth);
    m_insertSpaces->setChecked(editor.insertSpaces);
    m_autoIndent->setChecked(editor.autoIndent);
    m_autoClose->setChecked(editor.autoCloseBrackets);
    m_autoComplete->setChecked(editor.autoComplete);
    m_folding->setChecked(editor.codeFolding);
    m_lineNumbers->setChecked(editor.lineNumbers);
    m_wordWrap->setChecked(editor.wordWrap);
    m_bracketMatching->setChecked(editor.bracketMatching);

    const CompilerSettings &compiler = m_settings->compiler;
    m_gccPath->setText(compiler.gccPath);
    m_gxxPath->setText(compiler.gxxPath);
    m_gdbPath->setText(compiler.gdbPath);
    selectValue(m_cStandard, compiler.cStandard);
    selectValue(m_cxxStandard, compiler.cxxStandard);
    selectValue(m_debugOptimization, compiler.debugOptimization);
    selectValue(m_releaseOptimization, compiler.releaseOptimization);
    m_warningFlags->setText(compiler.warningFlags);
    m_includeDirs->setPlainText(compiler.includeDirs.join(QLatin1Char('\n')));
    m_libraryDirs->setPlainText(compiler.libraryDirs.join(QLatin1Char('\n')));
    m_libraries->setText(compiler.libraries);
    m_additionalArgs->setText(compiler.additionalArgs);
    m_outputDirectory->setText(compiler.outputDirectory);

    const LeetCodeSettings &leetcode = m_settings->leetcode;
    m_lcDirectory->setText(leetcode.problemsDirectory);
    m_lcStandard->setCurrentText(leetcode.standard);
    m_lcFlags->setText(leetcode.flags);
    m_lcTimeout->setValue(leetcode.timeoutMs);
    m_lcSanitizers->setChecked(leetcode.sanitizers);
}

void SettingsDialog::applyToSettings()
{
    AppearanceSettings &appearance = m_settings->appearance;
    appearance.theme = m_theme->currentData().toString() == QLatin1String("light")
                           ? ThemeKind::Light
                           : ThemeKind::Dark;
    appearance.fontFamily = m_fontFamily->currentFont().family();
    appearance.fontSize = m_fontSize->value();
    appearance.editorBackground =
        m_background->color().isValid() ? m_background->color().name() : QString();
    appearance.syntaxColors.clear();
    for (auto it = m_syntaxButtons.cbegin(); it != m_syntaxButtons.cend(); ++it) {
        if (it.value()->color().isValid())
            appearance.syntaxColors.insert(it.key(), it.value()->color().name());
    }
    appearance.sidebarVisible = m_sidebarVisible->isChecked();
    appearance.bottomPanelVisible = m_panelVisible->isChecked();

    EditorSettings &editor = m_settings->editor;
    editor.tabWidth = m_tabWidth->value();
    editor.insertSpaces = m_insertSpaces->isChecked();
    editor.autoIndent = m_autoIndent->isChecked();
    editor.autoCloseBrackets = m_autoClose->isChecked();
    editor.autoComplete = m_autoComplete->isChecked();
    editor.codeFolding = m_folding->isChecked();
    editor.lineNumbers = m_lineNumbers->isChecked();
    editor.wordWrap = m_wordWrap->isChecked();
    editor.bracketMatching = m_bracketMatching->isChecked();

    CompilerSettings &compiler = m_settings->compiler;
    compiler.gccPath = m_gccPath->text().trimmed();
    compiler.gxxPath = m_gxxPath->text().trimmed();
    compiler.gdbPath = m_gdbPath->text().trimmed();
    compiler.cStandard = m_cStandard->currentData().toString();
    compiler.cxxStandard = m_cxxStandard->currentData().toString();
    compiler.debugOptimization = m_debugOptimization->currentText();
    compiler.releaseOptimization = m_releaseOptimization->currentText();
    compiler.warningFlags = m_warningFlags->text().trimmed();
    compiler.includeDirs = nonEmptyLines(m_includeDirs->toPlainText());
    compiler.libraryDirs = nonEmptyLines(m_libraryDirs->toPlainText());
    compiler.libraries = m_libraries->text().trimmed();
    compiler.additionalArgs = m_additionalArgs->text().trimmed();
    compiler.outputDirectory = m_outputDirectory->text().trimmed();

    LeetCodeSettings &leetcode = m_settings->leetcode;
    leetcode.problemsDirectory = m_lcDirectory->text().trimmed();
    leetcode.standard = m_lcStandard->currentText();
    leetcode.flags = m_lcFlags->text().trimmed();
    leetcode.timeoutMs = m_lcTimeout->value();
    leetcode.sanitizers = m_lcSanitizers->isChecked();

    m_settings->save();
    emit m_settings->changed();
}
