#pragma once

#include "core/Settings.h"

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

struct ThemeColors
{
    QColor window;
    QColor surface;
    QColor surfaceRaised;
    QColor border;
    QColor text;
    QColor textMuted;
    QColor accent;
    QColor accentText;
    QColor selection;
    QColor editorBackground;
    QColor editorText;
    QColor currentLine;
    QColor gutterText;
    QColor error;
    QColor warning;
    QColor note;
    QColor success;
    QColor executionLine;
};

namespace Theme {

const ThemeColors &colors(ThemeKind kind);

// Applies the palette and style sheet to the whole application.
void apply(QApplication &application, ThemeKind kind);

// An icon from resources/icons, drawn in the given colour. Disabled states are
// generated too, so that callers only ever supply one colour.
QIcon icon(const QString &name, const QColor &color);

} // namespace Theme
