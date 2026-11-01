#pragma once

#include "core/Settings.h"

#include <QDialog>
#include <QHash>

class ColorButton;
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum Page { AppearancePage, EditorPage, CompilerPage };

    explicit SettingsDialog(Settings *settings, QWidget *parent = nullptr);

    void showPage(Page page);

    // Copies the dialog's values into the settings and persists them.
    void applyToSettings();

private:
    QWidget *createAppearancePage();
    QWidget *createEditorPage();
    QWidget *createCompilerPage();
    void loadFromSettings();

    Settings *m_settings;
    QTabWidget *m_tabs;

    QComboBox *m_theme;
    QFontComboBox *m_fontFamily;
    QSpinBox *m_fontSize;
    ColorButton *m_background;
    QHash<QString, ColorButton *> m_syntaxButtons;
    QCheckBox *m_sidebarVisible;
    QCheckBox *m_panelVisible;

    QSpinBox *m_tabWidth;
    QCheckBox *m_insertSpaces;
    QCheckBox *m_autoIndent;
    QCheckBox *m_autoClose;
    QCheckBox *m_autoComplete;
    QCheckBox *m_folding;
    QCheckBox *m_lineNumbers;
    QCheckBox *m_wordWrap;
    QCheckBox *m_bracketMatching;

    QLineEdit *m_gccPath;
    QLineEdit *m_gxxPath;
    QLineEdit *m_gdbPath;
    QComboBox *m_cStandard;
    QComboBox *m_cxxStandard;
    QComboBox *m_debugOptimization;
    QComboBox *m_releaseOptimization;
    QLineEdit *m_warningFlags;
    QPlainTextEdit *m_includeDirs;
    QPlainTextEdit *m_libraryDirs;
    QLineEdit *m_libraries;
    QLineEdit *m_additionalArgs;
    QLineEdit *m_outputDirectory;
};
