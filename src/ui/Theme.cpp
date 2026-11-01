#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QSvgRenderer>

// Resources in a static library are only linked when something references them.
void initResources()
{
    Q_INIT_RESOURCE(lightide);
}

namespace {

const ThemeColors darkColors = {
    /* window */ QColor("#15181e"),
    /* surface */ QColor("#1b1f27"),
    /* surfaceRaised */ QColor("#252a34"),
    /* border */ QColor("#2c323d"),
    /* text */ QColor("#d8dde6"),
    /* textMuted */ QColor("#8b94a5"),
    /* accent */ QColor("#4c9dff"),
    /* accentText */ QColor("#0b1320"),
    /* selection */ QColor("#2f4a6d"),
    /* editorBackground */ QColor("#14171c"),
    /* editorText */ QColor("#d8dde6"),
    /* currentLine */ QColor("#1c2029"),
    /* gutterText */ QColor("#5a6474"),
    /* error */ QColor("#f4635c"),
    /* warning */ QColor("#dfa83b"),
    /* note */ QColor("#62a7ff"),
    /* success */ QColor("#48bb78"),
    /* executionLine */ QColor("#3a3517"),
};

const ThemeColors lightColors = {
    /* window */ QColor("#eef0f3"),
    /* surface */ QColor("#f7f8fa"),
    /* surfaceRaised */ QColor("#ffffff"),
    /* border */ QColor("#d6dae1"),
    /* text */ QColor("#1f2430"),
    /* textMuted */ QColor("#5f6878"),
    /* accent */ QColor("#0d6fdb"),
    /* accentText */ QColor("#ffffff"),
    /* selection */ QColor("#bcd8fb"),
    /* editorBackground */ QColor("#ffffff"),
    /* editorText */ QColor("#1f2430"),
    /* currentLine */ QColor("#f3f5f8"),
    /* gutterText */ QColor("#9aa3b2"),
    /* error */ QColor("#d1242f"),
    /* warning */ QColor("#a66a00"),
    /* note */ QColor("#0d6fdb"),
    /* success */ QColor("#1a7f37"),
    /* executionLine */ QColor("#fff3c4"),
};

QPalette paletteFor(const ThemeColors &c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.surface);
    p.setColor(QPalette::AlternateBase, c.surfaceRaised);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::PlaceholderText, c.textMuted);
    p.setColor(QPalette::Button, c.surfaceRaised);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, c.error);
    p.setColor(QPalette::ToolTipBase, c.surfaceRaised);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::Highlight, c.selection);
    p.setColor(QPalette::HighlightedText, c.text);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::LinkVisited, c.accent);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border);
    p.setColor(QPalette::Disabled, QPalette::Text, c.textMuted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.textMuted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.textMuted);
    return p;
}

QString styleSheetFor(const ThemeColors &c, ThemeKind kind)
{
    QString sheet = QStringLiteral(R"(
* { outline: none; }

QMainWindow, QDialog { background: @window; }
QMainWindow::separator { background: @border; width: 1px; height: 1px; }

QMenuBar { background: @window; color: @text; padding: 2px 4px; border: none; }
QMenuBar::item { background: transparent; padding: 4px 9px; border-radius: 5px; }
QMenuBar::item:selected { background: @surfaceRaised; }
QMenu { background: @surfaceRaised; color: @text; border: 1px solid @border; padding: 5px; }
QMenu::item { padding: 5px 26px 5px 12px; border-radius: 4px; }
QMenu::item:selected { background: @selection; }
QMenu::item:disabled { color: @textMuted; }
QMenu::separator { height: 1px; background: @border; margin: 5px 6px; }
QMenu::icon { padding-left: 8px; }

QToolBar { background: @window; border: none; border-bottom: 1px solid @border; padding: 4px 8px; spacing: 2px; }
QToolBar::separator { background: @border; width: 1px; margin: 5px 7px; }
QToolButton { background: transparent; border: none; border-radius: 6px; padding: 5px; color: @text; }
QToolButton:hover { background: @surfaceRaised; }
QToolButton:pressed { background: @selection; }
QToolButton:checked { background: @selection; }

QTabWidget::pane { border: none; }
QTabBar { background: @window; }
QTabBar::tab { background: transparent; color: @textMuted; padding: 7px 14px; border: none;
               border-bottom: 2px solid transparent; }
QTabBar::tab:hover { color: @text; background: @surface; }
QTabBar::tab:selected { color: @text; background: @editorBackground; border-bottom: 2px solid @accent; }
QTabBar::close-button { image: url(:/icons/@closeIcon.svg); subcontrol-position: right;
                        border-radius: 3px; padding: 1px; }
QTabBar::close-button:hover { background: @surfaceRaised; }

QTreeView, QTreeWidget, QListView, QListWidget {
    background: @surface; color: @text; border: none; alternate-background-color: @surface;
    selection-background-color: @selection; selection-color: @text; }
QTreeView::item, QListView::item { padding: 3px 2px; }
QTreeView::item:hover, QListView::item:hover { background: @surfaceRaised; }
QTreeView::item:selected, QListView::item:selected { background: @selection; color: @text; }
QHeaderView::section { background: @surface; color: @textMuted; border: none;
                       border-bottom: 1px solid @border; padding: 4px 8px; }

QPlainTextEdit, QTextEdit { background: @editorBackground; color: @text; border: none;
                            selection-background-color: @selection; selection-color: @text; }

QLineEdit, QSpinBox, QComboBox {
    background: @surfaceRaised; color: @text; border: 1px solid @border; border-radius: 5px;
    padding: 4px 7px; selection-background-color: @selection; selection-color: @text; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: @accent; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: @surfaceRaised; border: 1px solid @border;
                              selection-background-color: @selection; }

QPushButton { background: @surfaceRaised; color: @text; border: 1px solid @border;
              border-radius: 5px; padding: 5px 14px; }
QPushButton:hover { border-color: @accent; }
QPushButton:pressed { background: @selection; }
QPushButton:default { background: @accent; color: @accentText; border-color: @accent; }
QPushButton:disabled { color: @textMuted; }

QGroupBox { border: 1px solid @border; border-radius: 6px; margin-top: 12px; padding: 10px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: @textMuted; }

QSplitter::handle { background: @border; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }

QStatusBar { background: @window; color: @textMuted; border-top: 1px solid @border; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @textMuted; padding: 0 7px; }

QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle { background: @border; border-radius: 4px; margin: 2px; }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::handle:hover { background: @textMuted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QToolTip { background: @surfaceRaised; color: @text; border: 1px solid @border; padding: 4px; }

QTabWidget#sidebar QTabBar::tab { padding: 7px 8px; }

QLabel#welcomeTitle { font-size: 26px; font-weight: 600; color: @text; }
QLabel#welcomeHint { color: @textMuted; }
QWidget#findBar { background: @surface; border-top: 1px solid @border; }
)");

    sheet.replace(QStringLiteral("@closeIcon"), kind == ThemeKind::Dark ? QStringLiteral("close-dark")
                                                                        : QStringLiteral("close-light"));

    const QHash<QString, QColor> tokens = {
        {QStringLiteral("@window"), c.window},
        {QStringLiteral("@surfaceRaised"), c.surfaceRaised},
        {QStringLiteral("@surface"), c.surface},
        {QStringLiteral("@border"), c.border},
        {QStringLiteral("@textMuted"), c.textMuted},
        {QStringLiteral("@text"), c.text},
        {QStringLiteral("@accentText"), c.accentText},
        {QStringLiteral("@accent"), c.accent},
        {QStringLiteral("@selection"), c.selection},
        {QStringLiteral("@editorBackground"), c.editorBackground},
    };
    // Longest first, so that @surface does not eat into @surfaceRaised.
    QStringList names = tokens.keys();
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.size() > b.size();
    });
    for (const QString &name : std::as_const(names))
        sheet.replace(name, tokens.value(name).name());
    return sheet;
}

QPixmap renderIcon(const QByteArray &svg, const QColor &color, int size)
{
    QByteArray tinted = svg;
    tinted.replace("currentColor", color.name().toLatin1());
    QSvgRenderer renderer(tinted);

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    return pixmap;
}

} // namespace

namespace Theme {

const ThemeColors &colors(ThemeKind kind)
{
    return kind == ThemeKind::Dark ? darkColors : lightColors;
}

void apply(QApplication &application, ThemeKind kind)
{
    initResources();
    const ThemeColors &c = colors(kind);
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    application.setPalette(paletteFor(c));
    application.setStyleSheet(styleSheetFor(c, kind));
}

QIcon icon(const QString &name, const QColor &color)
{
    static QHash<QString, QIcon> cache;
    const QString key = name + color.name();
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd())
        return *cached;

    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray svg = file.readAll();

    QColor disabled = color;
    disabled.setAlphaF(0.35f);

    QIcon icon;
    for (const int size : {16, 18, 20, 24, 32, 48}) {
        icon.addPixmap(renderIcon(svg, color, size), QIcon::Normal);
        icon.addPixmap(renderIcon(svg, disabled, size), QIcon::Disabled);
    }
    cache.insert(key, icon);
    return icon;
}

} // namespace Theme
