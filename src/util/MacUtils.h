#pragma once

#include <QColor>
class QWidget;

namespace MacUtils {
void applyTitleBarStyle(QWidget *window, bool dark, const QColor &bgColor);
}
