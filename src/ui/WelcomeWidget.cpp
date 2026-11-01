#include "ui/WelcomeWidget.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

WelcomeWidget::WelcomeWidget(QWidget *parent)
    : QWidget(parent)
    , m_recentLabel(new QLabel(tr("Recent files")))
    , m_recent(new QListWidget)
{
    auto *title = new QLabel(tr("LightIDE"));
    title->setObjectName(QStringLiteral("welcomeTitle"));
    auto *hint = new QLabel(
        tr("Open a file and press F11 to compile and run it. No project needed."));
    hint->setObjectName(QStringLiteral("welcomeHint"));

    auto *newCpp = new QPushButton(tr("New C++ File"));
    auto *newC = new QPushButton(tr("New C File"));
    auto *open = new QPushButton(tr("Open File..."));
    auto *openFolder = new QPushButton(tr("Open Folder..."));
    newCpp->setDefault(true);
    connect(newCpp, &QPushButton::clicked, this, &WelcomeWidget::newCppRequested);
    connect(newC, &QPushButton::clicked, this, &WelcomeWidget::newCRequested);
    connect(open, &QPushButton::clicked, this, &WelcomeWidget::openRequested);
    connect(openFolder, &QPushButton::clicked, this, &WelcomeWidget::openFolderRequested);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(newCpp);
    buttons->addWidget(newC);
    buttons->addWidget(open);
    buttons->addWidget(openFolder);
    buttons->addStretch();

    m_recentLabel->setObjectName(QStringLiteral("welcomeHint"));
    m_recent->setMaximumHeight(240);
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        emit recentActivated(item->data(Qt::UserRole).toString());
    });
    connect(m_recent, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        emit recentActivated(item->data(Qt::UserRole).toString());
    });

    auto *content = new QWidget;
    content->setMaximumWidth(620);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setSpacing(10);
    contentLayout->addWidget(title);
    contentLayout->addWidget(hint);
    contentLayout->addSpacing(12);
    contentLayout->addLayout(buttons);
    contentLayout->addSpacing(18);
    contentLayout->addWidget(m_recentLabel);
    contentLayout->addWidget(m_recent);
    contentLayout->addStretch(1);

    auto *outer = new QHBoxLayout(this);
    outer->addStretch(1);
    outer->addWidget(content, 3);
    outer->addStretch(1);
}

void WelcomeWidget::setRecentFiles(const QStringList &paths)
{
    m_recent->clear();
    for (const QString &path : paths) {
        auto *item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        m_recent->addItem(item);
    }
    m_recentLabel->setVisible(!paths.isEmpty());
    m_recent->setVisible(!paths.isEmpty());
}
