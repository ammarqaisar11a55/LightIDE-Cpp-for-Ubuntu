#pragma once

#include "core/Settings.h"
#include "project/Project.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

// Arguments, working directory and output name for one source file.
class RunOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RunOptionsDialog(const FileRunOptions &options, QWidget *parent = nullptr);
    FileRunOptions options() const;

private:
    QLineEdit *m_arguments;
    QLineEdit *m_workingDirectory;
    QLineEdit *m_outputName;
};

class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(const QString &defaultLocation, QWidget *parent = nullptr);

    QString projectName() const;
    QString projectDirectory() const;   // The location plus the project's own folder.
    bool isCpp() const;

private:
    QLineEdit *m_name;
    QLineEdit *m_location;
    QComboBox *m_language;
};

class ProjectOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectOptionsDialog(const Project &project, QWidget *parent = nullptr);
    void applyTo(Project &project) const;

private:
    QLineEdit *m_target;
    QComboBox *m_targetType;
    QComboBox *m_cStandard;
    QComboBox *m_cxxStandard;
    QComboBox *m_debugOptimization;
    QComboBox *m_releaseOptimization;
    QLineEdit *m_warningFlags;
    QLineEdit *m_defines;
    QLineEdit *m_includeDirs;
    QLineEdit *m_libraryDirs;
    QLineEdit *m_libraries;
    QLineEdit *m_extraArgs;
    QLineEdit *m_runArguments;
    QLineEdit *m_workingDirectory;
};
