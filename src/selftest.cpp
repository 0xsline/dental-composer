// 离屏自测：遍历全部布局模板，装入合成图片 + 文字，导出 2 倍 PNG 并校验尺寸与内容。
// 用法：dental-composer --selftest /tmp/out.png
#include "selftest.h"

#include "canvasview.h"
#include "mainwindow.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFont>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QPoint>
#include <QString>
#include <QUrl>

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
        if (canvas->isFreeLayout()) {
            if (zones.size() != 1) {
                std::fprintf(stderr, "SELFTEST FAIL: free layout %d has %d zones\n",
                             layout, int(zones.size()));
                return 5;
            }
            canvas->addFreeImage(source[0], QString(), QPointF(400.0, 400.0));
            canvas->addFreeImage(source[1], QString(), QPointF(1000.0, 400.0));
            canvas->addFreeImage(source[2], QString(), QPointF(700.0, 750.0));
            if (canvas->freeImageCount() != 3) {
                std::fprintf(stderr, "SELFTEST FAIL: free layout expected 3 images, got %d\n",
                             canvas->freeImageCount());
                return 46;
            }
        } else {
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
        }

        canvas->addText(QString::fromUtf8("示例患者  13800000000  7Y 8M"));
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
        if (canvas->isFreeLayout()) {
            const QPoint probe(800, 800); // 第一张自由图中心附近
            if (check.pixelColor(probe) == QColor(Qt::white)) {
                std::fprintf(stderr, "SELFTEST FAIL: blank free collage at %d,%d\n",
                             probe.x(), probe.y());
                return 3;
            }
        } else {
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
        }
        // 画布外缘边距必须纯白（各布局分区都不触及边距）
        if (check.pixelColor(20, 1100) != QColor(Qt::white)) {
            std::fprintf(stderr, "SELFTEST FAIL: margin not white (layout %d)\n", layout);
            return 4;
        }
    }

    // 大图降采样路径：6000x4000 超出分区显示上限，导入后导出应正常且像素完整
    canvas->applyLayout(0);
    QImage big(6000, 4000, QImage::Format_RGB32);
    big.fill(QColor(0x7d, 0x5a, 0x9e));
    QElapsedTimer timer;
    timer.start();
    canvas->zones().at(0)->setImage(big);
    const QString bigOut = dir + QStringLiteral("/") + base + QStringLiteral("_big.png");
    if (!canvas->exportImage(bigOut, 3.0)) {
        std::fprintf(stderr, "SELFTEST FAIL: big image export error\n");
        return 7;
    }
    const qint64 elapsed = timer.elapsed();
    const QImage bigCheck(bigOut);
    if (bigCheck.isNull() || bigCheck.width() != 4800 || bigCheck.height() != 3300) {
        std::fprintf(stderr, "SELFTEST FAIL: big export unexpected size %dx%d\n",
                     bigCheck.width(), bigCheck.height());
        return 8;
    }
    if (bigCheck.pixelColor(1227, 1389) == QColor(Qt::white)) {
        std::fprintf(stderr, "SELFTEST FAIL: big image zone blank\n");
        return 9;
    }
    // 工程文件往返：先落盘样图（工程保存记录路径），再保存→清空→打开→导出校验
    for (int i = 0; i < 4; ++i)
        source[i].save(dir + QStringLiteral("/") + base + QStringLiteral("_src%1.png").arg(i + 1));
    canvas->applyLayout(0);
    for (int i = 0; i < 4; ++i)
        canvas->zones().at(i)->setImage(source[i],
            dir + QStringLiteral("/") + base + QStringLiteral("_src%1.png").arg(i + 1));
    canvas->setTemplateText({ QStringLiteral("8.18号，示例患者，袁萍医生，初诊"),
                              QStringLiteral("7Y 8M，主诉：窝沟复查") });
    canvas->addText(QString::fromUtf8("工程往返测试文字"));
    const QString projPath = dir + QStringLiteral("/") + base + QStringLiteral("_project.dcp");
    if (!canvas->saveProject(projPath)) {
        std::fprintf(stderr, "SELFTEST FAIL: save project error\n");
        return 10;
    }
    canvas->clearContent();
    if (!canvas->loadProject(projPath)) {
        std::fprintf(stderr, "SELFTEST FAIL: load project error\n");
        return 11;
    }
    const QString projOut = dir + QStringLiteral("/") + base + QStringLiteral("_project.png");
    if (!canvas->exportImage(projOut, 2.0)) {
        std::fprintf(stderr, "SELFTEST FAIL: project export error\n");
        return 12;
    }
    const QImage projCheck(projOut);
    if (projCheck.isNull() || projCheck.width() != 3200 || projCheck.height() != 2200) {
        std::fprintf(stderr, "SELFTEST FAIL: project export unexpected size\n");
        return 13;
    }
    if (projCheck.pixelColor(818, 1000) == QColor(Qt::white)) {
        std::fprintf(stderr, "SELFTEST FAIL: project zone blank\n");
        return 14;
    }

    // PDF 导出
    const QString pdfPath = dir + QStringLiteral("/") + base + QStringLiteral("_out.pdf");
    if (!canvas->exportPdf(pdfPath)) {
        std::fprintf(stderr, "SELFTEST FAIL: pdf export error\n");
        return 15;
    }
    if (QFileInfo(pdfPath).size() < 1000) {
        std::fprintf(stderr, "SELFTEST FAIL: pdf too small\n");
        return 16;
    }
    // 拖边缩放：句柄在图片上，等比例自由缩放，可小于分区
    canvas->applyLayout(0);
    PhotoZone *rz = canvas->zones().at(0);
    rz->setImage(source[0]);
    PhotoItem *ritem = rz->item();
    ritem->setSelected(true);
    const qreal scaleFit = ritem->scale();
    const QRectF img0 = ritem->imageSceneRect();
    ritem->applyResize(PhotoItem::ResizeHandle::CornerBottomRight,
                       img0.bottomRight() + QPointF(200.0, 200.0));
    if (ritem->scale() <= scaleFit + 0.001) {
        std::fprintf(stderr, "SELFTEST FAIL: resize did not enlarge (%.3f vs %.3f)\n",
                     ritem->scale(), scaleFit);
        return 18;
    }
    ritem->applyResize(PhotoItem::ResizeHandle::CornerBottomRight, img0.center());
    if (ritem->scale() >= scaleFit) {
        std::fprintf(stderr, "SELFTEST FAIL: resize could not shrink below fit (%.3f vs %.3f)\n",
                     ritem->scale(), scaleFit);
        return 23;
    }
    ritem->setSelected(false);

    // 撤销：导入 → 撤销 → 图片移除
    canvas->applyLayout(0);
    const QString src1 = dir + QStringLiteral("/") + base + QStringLiteral("_src1.png");
    canvas->importImages({ src1 });
    if (!canvas->zones().at(0)->hasImage()) {
        std::fprintf(stderr, "SELFTEST FAIL: import did not load\n");
        return 19;
    }
    canvas->undo();
    if (canvas->zones().at(0)->hasImage()) {
        std::fprintf(stderr, "SELFTEST FAIL: undo did not remove imported image\n");
        return 20;
    }

    // 滚轮缩放路径：快照 → zoomBy 放大 → 撤销恢复原比例
    canvas->zones().at(0)->setImage(source[0], src1);
    PhotoItem *zi = canvas->zones().at(0)->item();
    const qreal zoomBefore = zi->scale();
    canvas->pushUndoSnapshot();
    zi->zoomBy(1.4, canvas->zones().at(0)->rect().center());
    if (zi->scale() <= zoomBefore) {
        std::fprintf(stderr, "SELFTEST FAIL: zoomBy did not zoom in\n");
        return 21;
    }
    canvas->undo();
    PhotoItem *ziRestored = canvas->zones().at(0)->item();
    if (!ziRestored || qAbs(ziRestored->scale() - zoomBefore) > 0.001) {
        std::fprintf(stderr, "SELFTEST FAIL: undo did not restore zoom\n");
        return 22;
    }

    // 等比例缩小：必须能小于初始 contain
    canvas->applyLayout(0);
    canvas->zones().at(0)->setImage(source[0], src1);
    PhotoItem *shrinkItem = canvas->zones().at(0)->item();
    const qreal shrinkBefore = shrinkItem->scale();
    shrinkItem->zoomBy(0.5, shrinkItem->imageSceneRect().center());
    if (shrinkItem->scale() >= shrinkBefore - 0.001) {
        std::fprintf(stderr, "SELFTEST FAIL: zoomBy could not shrink below contain (%.3f vs %.3f)\n",
                     shrinkItem->scale(), shrinkBefore);
        return 24;
    }
    if (qAbs(shrinkItem->scale() / shrinkBefore - 0.5) > 0.05) {
        std::fprintf(stderr, "SELFTEST FAIL: zoomBy not proportional (%.3f -> %.3f)\n",
                     shrinkBefore, shrinkItem->scale());
        return 25;
    }

    // 拖入：MIME 解析 + 发送 Drop 事件
    canvas->applyLayout(0);
    QMimeData mime;
    mime.setUrls({ QUrl::fromLocalFile(src1) });
    if (!CanvasView::canAcceptImageDrop(&mime)) {
        std::fprintf(stderr, "SELFTEST FAIL: canAcceptImageDrop rejected local file\n");
        return 26;
    }
    const QStringList parsed = CanvasView::imagePathsFromMime(&mime);
    if (parsed.size() != 1 || parsed.at(0) != src1) {
        std::fprintf(stderr, "SELFTEST FAIL: imagePathsFromMime missed local file\n");
        return 27;
    }
    QMimeData fileIdMime;
    fileIdMime.setUrls({ QUrl(QStringLiteral("file:///.file/id=6571367.1")) });
    if (!CanvasView::canAcceptImageDrop(&fileIdMime)) {
        std::fprintf(stderr, "SELFTEST FAIL: canAcceptImageDrop rejected file-id URL (Finder drag)\n");
        return 28;
    }
    PhotoZone *dropZone = canvas->zones().at(0);
    const QPoint dropPt = canvas->mapFromScene(dropZone->rect().center());
    QWidget *target = canvas->viewport();
    QDragEnterEvent enter(dropPt, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &enter);
    if (!enter.isAccepted()) {
        std::fprintf(stderr, "SELFTEST FAIL: dragEnter rejected local image\n");
        return 29;
    }
    QDropEvent drop(dropPt, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &drop);
    if (!dropZone->hasImage()) {
        std::fprintf(stderr, "SELFTEST FAIL: dropEvent did not import image\n");
        return 30;
    }

    // 旋转 90°：宽高对调，撤销恢复；工程文件记下 rotation
    canvas->applyLayout(0);
    canvas->zones().at(0)->setImage(source[0], src1);
    PhotoItem *rotItem = canvas->zones().at(0)->item();
    const int w0 = rotItem->image().width();
    const int h0 = rotItem->image().height();
    rotItem->setSelected(true);
    canvas->rotateSelected(1);
    rotItem = canvas->zones().at(0)->item();
    if (!rotItem || rotItem->image().width() != h0 || rotItem->image().height() != w0
        || rotItem->rotationDegrees() != 90) {
        std::fprintf(stderr, "SELFTEST FAIL: rotate 90 did not swap size (rot=%d %dx%d)\n",
                     rotItem ? rotItem->rotationDegrees() : -1,
                     rotItem ? rotItem->image().width() : 0,
                     rotItem ? rotItem->image().height() : 0);
        return 31;
    }
    canvas->undo();
    rotItem = canvas->zones().at(0)->item();
    if (!rotItem || rotItem->image().width() != w0 || rotItem->rotationDegrees() != 0) {
        std::fprintf(stderr, "SELFTEST FAIL: undo did not restore rotation\n");
        return 32;
    }
    rotItem->setSelected(true);
    canvas->rotateSelected(1);
    const QString rotProj = dir + QStringLiteral("/") + base + QStringLiteral("_rot.dcp");
    if (!canvas->saveProject(rotProj) || !canvas->loadProject(rotProj)) {
        std::fprintf(stderr, "SELFTEST FAIL: rotate project roundtrip io\n");
        return 33;
    }
    rotItem = canvas->zones().at(0)->item();
    if (!rotItem || rotItem->rotationDegrees() != 90
        || rotItem->image().width() != h0 || rotItem->image().height() != w0) {
        std::fprintf(stderr, "SELFTEST FAIL: project did not restore rotation\n");
        return 34;
    }

    // 复制拼图：2× 必须是 3200×2200
    canvas->applyLayout(0);
    canvas->zones().at(0)->setImage(source[0], src1);
    const QImage raster = canvas->renderToImage(2.0);
    if (raster.width() != 3200 || raster.height() != 2200) {
        std::fprintf(stderr, "SELFTEST FAIL: renderToImage size %dx%d\n",
                     raster.width(), raster.height());
        return 35;
    }
    if (!canvas->copyImageToClipboard(2.0)) {
        std::fprintf(stderr, "SELFTEST FAIL: copyImageToClipboard failed\n");
        return 36;
    }
    const QImage clipped = QGuiApplication::clipboard()->image();
    if (clipped.width() != 3200 || clipped.height() != 2200) {
        std::fprintf(stderr, "SELFTEST FAIL: clipboard image size %dx%d\n",
                     clipped.width(), clipped.height());
        return 37;
    }

    // 内嵌工程：删掉源图后仍能打开
    canvas->applyLayout(0);
    for (int i = 0; i < 4; ++i)
        canvas->zones().at(i)->setImage(source[i],
            dir + QStringLiteral("/") + base + QStringLiteral("_src%1.png").arg(i + 1));
    const QString packedPath = dir + QStringLiteral("/") + base + QStringLiteral("_packed.dcp");
    if (!canvas->saveProject(packedPath)) {
        std::fprintf(stderr, "SELFTEST FAIL: save packed project\n");
        return 38;
    }
    {
        const QByteArray packed = [&]() {
            QFile f(packedPath);
            if (!f.open(QIODevice::ReadOnly))
                return QByteArray();
            return f.readAll();
        }();
        if (!packed.contains("imageData")) {
            std::fprintf(stderr, "SELFTEST FAIL: packed project missing imageData\n");
            return 39;
        }
    }
    for (int i = 0; i < 4; ++i)
        QFile::remove(dir + QStringLiteral("/") + base + QStringLiteral("_src%1.png").arg(i + 1));
    canvas->clearContent();
    if (!canvas->loadProject(packedPath)) {
        std::fprintf(stderr, "SELFTEST FAIL: load packed project after sources deleted\n");
        return 40;
    }
    if (!canvas->zones().at(0)->hasImage() || !canvas->zones().at(3)->hasImage()) {
        std::fprintf(stderr, "SELFTEST FAIL: packed project lost images\n");
        return 41;
    }
    const QString packedOut = dir + QStringLiteral("/") + base + QStringLiteral("_packed.png");
    if (!canvas->exportImage(packedOut, 2.0)) {
        std::fprintf(stderr, "SELFTEST FAIL: packed export\n");
        return 42;
    }
    const QImage packedCheck(packedOut);
    if (packedCheck.isNull() || packedCheck.pixelColor(818, 1000) == QColor(Qt::white)) {
        std::fprintf(stderr, "SELFTEST FAIL: packed export blank\n");
        return 43;
    }

    // 旧版只记路径的 .dcp 仍能打开（源图还在）
    source[0].save(dir + QStringLiteral("/") + base + QStringLiteral("_src1.png"));
    QJsonObject v1root;
    v1root.insert(QStringLiteral("version"), 1);
    v1root.insert(QStringLiteral("layout"), 0);
    QJsonArray v1zones;
    QJsonObject z0;
    z0.insert(QStringLiteral("image"), dir + QStringLiteral("/") + base + QStringLiteral("_src1.png"));
    z0.insert(QStringLiteral("x"), 28.0);
    z0.insert(QStringLiteral("y"), 28.0);
    z0.insert(QStringLiteral("scale"), 1.0);
    v1zones.append(z0);
    v1root.insert(QStringLiteral("zones"), v1zones);
    const QString v1Path = dir + QStringLiteral("/") + base + QStringLiteral("_v1.dcp");
    {
        QFile f(v1Path);
        if (!f.open(QIODevice::WriteOnly)
            || f.write(QJsonDocument(v1root).toJson(QJsonDocument::Compact)) <= 0) {
            std::fprintf(stderr, "SELFTEST FAIL: write v1 project\n");
            return 44;
        }
    }
    canvas->clearContent();
    if (!canvas->loadProject(v1Path) || !canvas->zones().at(0)->hasImage()) {
        std::fprintf(stderr, "SELFTEST FAIL: old path-only dcp did not load\n");
        return 45;
    }

    // 自由拼图工程往返
    canvas->applyLayout(canvas->layoutCount() - 1);
    if (!canvas->isFreeLayout()) {
        std::fprintf(stderr, "SELFTEST FAIL: last layout is not freeform\n");
        return 47;
    }
    canvas->addFreeImage(source[0], QString(), QPointF(500.0, 400.0));
    canvas->addFreeImage(source[1], QString(), QPointF(1100.0, 700.0));
    const QString freeProj = dir + QStringLiteral("/") + base + QStringLiteral("_free.dcp");
    if (!canvas->saveProject(freeProj)) {
        std::fprintf(stderr, "SELFTEST FAIL: save free project\n");
        return 48;
    }
    canvas->clearContent();
    if (!canvas->loadProject(freeProj) || !canvas->isFreeLayout() || canvas->freeImageCount() != 2) {
        std::fprintf(stderr, "SELFTEST FAIL: free project roundtrip count=%d\n",
                     canvas->freeImageCount());
        return 49;
    }

    std::printf("SELFTEST OK %s (%d layouts, project+pdf+resize+undo+drop+rotate+copy+embed+free)\n",
                qPrintable(outPath), canvas->layoutCount());
    return 0;
}
