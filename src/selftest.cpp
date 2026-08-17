// 离屏自测：遍历全部布局模板，装入合成图片 + 文字，导出 2 倍 PNG 并校验尺寸与内容。
// 用法：dental-composer --selftest /tmp/out.png
#include "selftest.h"

#include "canvasview.h"
#include "mainwindow.h"

#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QString>

#include <cstdio>

int runSelftest(const QString &outPath)
{
    MainWindow window;
    CanvasView *canvas = window.canvas();
    // 默认布局必须是第一个模板（四格）
    if (canvas->currentLayout() != 0) {
        std::fprintf(stderr, "SELFTEST FAIL: default layout is %d\n", canvas->currentLayout());
        return 6;
    }
    const char *names[5] = { "全景片", "正面照", "上颌", "下颌", "上下颌" };
    const char *colors[5] = { "#5b6b7f", "#c9a03a", "#3f9d5f", "#b04a3f", "#6b5b8e" };
    const QFileInfo outInfo(outPath);
    const QString dir = outInfo.absolutePath();
    const QString base = outInfo.completeBaseName();

    QImage source[5];
    for (int i = 0; i < 5; ++i) {
        const QSize size = (i == 4) ? QSize(1600, 600) : QSize(800, 600);
        source[i] = QImage(size, QImage::Format_RGB32);
        source[i].fill(QColor(QString::fromLatin1(colors[i])));
        QPainter painter(&source[i]);
        painter.setPen(Qt::white);
        QFont font;
        font.setPixelSize(48);
        painter.setFont(font);
        painter.drawText(source[i].rect(), Qt::AlignCenter, QString::fromUtf8(names[i]));
        painter.end();
    }

    for (int layout = 0; layout < canvas->layoutCount(); ++layout) {
        canvas->applyLayout(layout);
        const QList<PhotoZone *> zones = canvas->zones();
        if (zones.size() < 2 || zones.size() > 4) {
            std::fprintf(stderr, "SELFTEST FAIL: layout %d has %d zones\n",
                         layout, int(zones.size()));
            return 5;
        }
        // 按分区标签匹配装入对应图片，保证导出图自洽
        for (PhotoZone *zone : zones) {
            int best = 0;
            for (int i = 0; i < 5; ++i) {
                if (zone->label() == QString::fromUtf8(names[i])) {
                    best = i;
                    break;
                }
            }
            zone->setImage(source[best]);
        }

        canvas->addText(QString::fromUtf8("陶言溪  18663779732  7Y 8M"));
        canvas->addText(QString::fromUtf8("主诉：窝沟复查"));

        const QString out = dir + QStringLiteral("/") + base
            + QStringLiteral("_layout%1.png").arg(layout);
        if (!canvas->exportImage(out, 2.0)) {
            std::fprintf(stderr, "SELFTEST FAIL: export error (layout %d)\n", layout);
            return 1;
        }
        const QImage check(out);
        if (check.isNull() || check.width() != 3200 || check.height() != 2200) {
            std::fprintf(stderr, "SELFTEST FAIL: unexpected size %dx%d (layout %d)\n",
                         check.width(), check.height(), layout);
            return 2;
        }
        // 每个分区取中心偏下 35% 高度处（避开图片中央白字），必须非纯白
        for (const PhotoZone *zone : zones) {
            const QRectF r = zone->rect();
            const QPoint probe(int(r.center().x() * 2.0),
                               int((r.center().y() + r.height() * 0.35) * 2.0));
            if (check.pixelColor(probe) == QColor(Qt::white)) {
                std::fprintf(stderr, "SELFTEST FAIL: blank zone in layout %d at %d,%d\n",
                             layout, probe.x(), probe.y());
                return 3;
            }
        }
        // 画布外缘边距必须纯白（各布局分区都不触及边距）
        if (check.pixelColor(20, 1100) != QColor(Qt::white)) {
            std::fprintf(stderr, "SELFTEST FAIL: margin not white (layout %d)\n", layout);
            return 4;
        }
    }

    // 保存单张样图供 GUI 拖入测试
    for (int i = 0; i < 4; ++i)
        source[i].save(dir + QStringLiteral("/") + base + QStringLiteral("_src%1.png").arg(i + 1));

    std::printf("SELFTEST OK %s (%d layouts)\n", qPrintable(outPath), canvas->layoutCount());
    return 0;
}
