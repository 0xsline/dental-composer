// Qt 5.15 / Qt 6 兼容宏：鼠标类事件坐标取值。
#pragma once

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define QT_MOUSE_POS(e) ((e)->position().toPoint())
#else
#define QT_MOUSE_POS(e) ((e)->pos())
#endif
