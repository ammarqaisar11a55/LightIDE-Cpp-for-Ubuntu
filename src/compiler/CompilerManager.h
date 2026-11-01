#pragma once

#include <QObject>
#include <QString>

struct CompilerSettings;

struct ToolInfo
{
    QString name;      // Display name: GCC, G++ or GDB.
    QString command;   // As configured, a bare name or a path.
    QString path;      // Resolved absolute path, empty when not found.
    QString version;
    bool found = false;
};

// Finds the system toolchain without blocking the GUI thread.
class CompilerManager : public QObject
{
    Q_OBJECT

public:
    explicit CompilerManager(QObject *parent = nullptr);

    void detect(const CompilerSettings &settings);

    const ToolInfo &gcc() const { return m_gcc; }
    const ToolInfo &gxx() const { return m_gxx; }
    const ToolInfo &gdb() const { return m_gdb; }
    bool detecting() const { return m_pending > 0; }

    static QString installHint();
    static QString parseVersion(const QString &versionOutput);

signals:
    void detectionFinished();

private:
    void probe(ToolInfo &tool, const QString &name, const QString &command);

    ToolInfo m_gcc;
    ToolInfo m_gxx;
    ToolInfo m_gdb;
    int m_pending = 0;
    int m_generation = 0;
};
