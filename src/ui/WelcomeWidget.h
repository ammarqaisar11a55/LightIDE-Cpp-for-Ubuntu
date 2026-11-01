#pragma once

#include <QWidget>

class QLabel;
class QListWidget;

// Shown in place of the tabs when no file is open.
class WelcomeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomeWidget(QWidget *parent = nullptr);

    void setRecentFiles(const QStringList &paths);

signals:
    void newCppRequested();
    void newCRequested();
    void openRequested();
    void recentActivated(const QString &path);

private:
    QLabel *m_recentLabel;
    QListWidget *m_recent;
};
