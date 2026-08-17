// 离屏自测：装入 4 张合成图片 + 2 条文字，导出 2 倍 PNG 并校验尺寸与内容。
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

    const char *names[4] = { "全景片", "正面照", "上颌", "下颌" };
    const char *colors[4] = { "#5b6b7f", "#c9a03a", "#3f9d5f", "#b04a3f" };
    const QFileInfo outInfo(outPath);
    const QString dir = outInfo.absolutePath();
    const QString base = outInfo.completeBaseName();

    for (int i = 0; i < 4; ++i) {
        QImage image(800, 600, QImage::Format_RGB32);
        image.fill(QColor(QString::fromLatin1(colors[i])));
        QPainter painter(&image);
        painter.setPen(Qt::white);
        QFont font;
        font.setPixelSize(48);
        painter.setFont(font);
        painter.drawText(image.rect(), Qt::AlignCenter, QString::fromUtf8(names[i]));
        painter.end();
        canvas->zones().at(i)->setImage(image);
        // 顺带保存单张样图，供 GUI 拖入测试使用
        image.save(dir + QStringLiteral("/") + base + QStringLiteral("_src%1.png").arg(i + 1));
    }

    canvas->addText(QString::fromUtf8("陶言溪  18663779732  7Y 8M"));
    canvas->addText(QString::fromUtf8("主诉：窝沟复查"));

    if (!canvas->exportImage(outPath, 2.0)) {
        std::fprintf(stderr, "SELFTEST FAIL: export error\n");
        return 1;
    }

    const QImage check(outPath);
    if (check.isNull() || check.width() != 3200 || check.height() != 2200) {
        std::fprintf(stderr, "SELFTEST FAIL: unexpected size %dx%d\n", check.width(), check.height());
        return 2;
    }

    // 每个分区取中心偏下一点（避开图片中央的白色文字），必须非纯白
    const QPoint probes[4] = {
        QPoint(818, 1000), QPoint(2382, 1000), QPoint(818, 2016), QPoint(2382, 2016)
    };
    for (int i = 0; i < 4; ++i) {
        if (check.pixelColor(probes[i]) == QColor(Qt::white)) {
            std::fprintf(stderr, "SELFTEST FAIL: zone %d is blank\n", i + 1);
            return 3;
        }
    }

    // 分区间隙必须为纯白（四格之间的十字区域）
    if (check.pixelColor(1600, 1100) != QColor(Qt::white)) {
        std::fprintf(stderr, "SELFTEST FAIL: zone gap is not white\n");
        return 4;
    }

    std::printf("SELFTEST OK %s (%dx%d)\n", qPrintable(outPath), check.width(), check.height());
    return 0;
}
