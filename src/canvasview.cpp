#include "canvasview.h"
#include "qtcompat.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QGraphicsSimpleTextItem>
#include <QImageReader>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>

#include <cmath>

namespace {

CanvasView *viewFor(QGraphicsItem *item)
{
    if (!item || !item->scene() || item->scene()->views().isEmpty())
        return nullptr;
    return qobject_cast<CanvasView *>(item->scene()->views().constFirst());
}
    // 护眼色系（豆沙绿主题）
    const QColor kSceneBg(0xc7, 0xed, 0xcc);       // 画布背景
    const QColor kZoneBg(0xe4, 0xf3, 0xe6);        // 分区底色
    const QColor kZoneBorder(0x9c, 0xc3, 0xa8);    // 空分区虚线边框
    const QColor kZoneBorderFull(0xbc, 0xd7, 0xc2); // 有图分区边框
    const QColor kZoneHint(0x7a, 0x96, 0x81);      // 占位提示文字
    const QColor kLabelText(0x3e, 0x5b, 0x46);     // 分区角标
    const QColor kViewBg(0xb3, 0xd8, 0xb8);        // 画布外留白

} // namespace

// ---------------------------------------------------------------------------
// PhotoItem
// ---------------------------------------------------------------------------

PhotoItem::PhotoItem(const QImage &original, const QImage &display, PhotoZone *zone)
    : QGraphicsPixmapItem(QPixmap::fromImage(display))
    , m_image(original)
    , m_zone(zone)
{
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setCursor(Qt::OpenHandCursor);
    setTransformationMode(Qt::SmoothTransformation);
    // 矩形图片用包围盒命中检测（免逐像素 mask）；拖动平移时用设备缓存，缩放时才重建
    setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    fitToZone();
}

qreal PhotoItem::fitScale() const
{
    if (!m_zone || pixmap().isNull())
        return 1.0;
    const QRectF zr = m_zone->rect();
    // 覆盖模式：取较大比例，图片始终铺满分区（无留白），可平移/缩放查看细节
    return qMax(zr.width() / pixmap().width(), zr.height() / pixmap().height());
}

qreal PhotoItem::maxScale() const
{
    return qMax(4.0 * fitScale(), 3.0);
}

void PhotoItem::fitToZone()
{
    if (!m_zone)
        return;
    const qreal s = fitScale();
    setScale(s);
    const QRectF zr = m_zone->rect();
    const QSizeF sz = QSizeF(pixmap().size()) * s;
    // PhotoItem 是顶层条目，坐标为场景坐标，须加上分区原点
    setPos(zr.topLeft() + QPointF((zr.width() - sz.width()) / 2.0, (zr.height() - sz.height()) / 2.0));
    clampToZone();
}

void PhotoItem::clampToZone()
{
    if (!m_zone)
        return;
    const QRectF zr = m_zone->rect();
    const QSizeF sz = QSizeF(pixmap().size()) * scale();
    const qreal minX = zr.right() - sz.width();
    const qreal minY = zr.bottom() - sz.height();
    QPointF p = pos();
    p.setX(qBound(minX, p.x(), zr.left()));
    p.setY(qBound(minY, p.y(), zr.top()));
    setPos(p);
}


QPainterPath PhotoItem::localClipPath() const
{
    QPainterPath path;
    if (m_zone)
        path.addPolygon(mapFromParent(QRectF(m_zone->rect())));
    return path;
}

void PhotoItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    painter->save();
    painter->setClipPath(localClipPath());
    QGraphicsPixmapItem::paint(painter, option, widget);
    painter->restore();
}

void PhotoItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastScenePos = event->scenePos();
        setCursor(Qt::ClosedHandCursor);
        setSelected(true);
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mousePressEvent(event);
}

void PhotoItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_dragging) {
        const QPointF delta = event->scenePos() - m_lastScenePos;
        m_lastScenePos = event->scenePos();
        setPos(pos() + delta);
        clampToZone();
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mouseMoveEvent(event);
}

void PhotoItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        if (CanvasView *view = viewFor(this))
            view->requestMoveBetweenZones(this, event->scenePos());
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mouseReleaseEvent(event);
}

void PhotoItem::wheelEvent(QGraphicsSceneWheelEvent *event)
{
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    const int dy = event->delta();
    QT_WARNING_POP
    if (dy == 0) {
        event->ignore();
        return;
    }
    const qreal factor = std::pow(1.15, dy / 120.0);
    const qreal s = qBound(fitScale(), scale() * factor, maxScale());
    setTransformOriginPoint(mapFromScene(event->scenePos()));
    setScale(s);
    clampToZone();
    event->accept();
}

void PhotoItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    fitToZone();
    event->accept();
}

// ---------------------------------------------------------------------------
// PhotoZone
// ---------------------------------------------------------------------------

PhotoZone::PhotoZone(const QRectF &rect, const QString &label, QGraphicsItem *parent)
    : QGraphicsRectItem(rect, parent)
    , m_label(label)
{
    setPen(Qt::NoPen);
}

void PhotoZone::setImage(const QImage &image, const QString &sourcePath)
{
    takeImage();
    // 显示副本按分区 3 倍上限降采样（最大缩放下像素级无损），原图保留用于导出
    QImage display = image;
    const QRectF zr = rect();
    const qreal ratio = qMin(zr.width() * 3.0 / image.width(), zr.height() * 3.0 / image.height());
    if (ratio < 1.0)
        display = image.scaled(QSize(int(image.width() * ratio), int(image.height() * ratio)),
                               Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_item = new PhotoItem(image, display, this);
    m_item->setZValue(0);
    m_item->setSourcePath(sourcePath);
    scene()->addItem(m_item);
}

QImage PhotoZone::takeImage()
{
    QImage image;
    if (m_item) {
        image = m_item->image();
        if (m_item->scene())
            m_item->scene()->removeItem(m_item);
        delete m_item;
        m_item = nullptr;
    }
    return image;
}

void PhotoZone::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect();
    QPainterPath path;
    path.addRoundedRect(r, 10.0, 10.0);
    painter->fillPath(path, kZoneBg);
    if (hasImage()) {
        painter->setPen(QPen(kZoneBorderFull, 1.0));
        painter->drawPath(path);
    } else {
        painter->setPen(QPen(kZoneBorder, 1.5, Qt::DashLine));
        painter->drawPath(path);
        QFont f = painter->font();
        f.setPixelSize(22);
        painter->setFont(f);
        painter->setPen(kZoneHint);
        painter->drawText(r, Qt::AlignCenter, QStringLiteral("拖入图片 或 双击导入"));
    }
}

// ---------------------------------------------------------------------------
// TextItem
// ---------------------------------------------------------------------------

TextItem::TextItem(const QString &text, QGraphicsItem *parent)
    : QGraphicsTextItem(text, parent)
{
    setTextInteractionFlags(Qt::NoTextInteraction);
    setCursor(Qt::ArrowCursor);
}

void TextItem::startEdit()
{
    setTextInteractionFlags(Qt::TextEditorInteraction);
    setFocus(Qt::MouseFocusReason);
    QTextCursor cursor = textCursor();
    cursor.select(QTextCursor::Document);
    setTextCursor(cursor);
}

void TextItem::focusOutEvent(QFocusEvent *event)
{
    QGraphicsTextItem::focusOutEvent(event);
    setTextInteractionFlags(Qt::NoTextInteraction);
    if (toPlainText().trimmed().isEmpty()) {
        QGraphicsScene *s = scene();
        if (s) {
            QTimer::singleShot(0, s, [s, this]() {
                s->removeItem(this);
                delete this;
            });
        }
    }
}

// ---------------------------------------------------------------------------
// CanvasView
// ---------------------------------------------------------------------------

CanvasView::CanvasView(QWidget *parent)
    : QGraphicsView(parent)
{
    setScene(&m_scene);
    buildScene();
    m_scene.setItemIndexMethod(QGraphicsScene::NoIndex);

    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setDragMode(QGraphicsView::NoDrag);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setAlignment(Qt::AlignCenter);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);

    setBackgroundBrush(kViewBg);
    m_scene.setBackgroundBrush(kSceneBg);
}

static const QList<LayoutSpec> &layoutTemplates()
{
    static const QList<LayoutSpec> templates = [] {
        QList<LayoutSpec> list;
        const qreal W = 1600.0;
        const qreal H = 1100.0;
        const qreal margin = 28.0;
        const qreal gap = 20.0;
        const qreal zw = (W - 2 * margin - gap) / 2.0;
        const qreal zh = (H - 2 * margin - gap) / 2.0;
        const qreal ww = W - 2 * margin;

        LayoutSpec s1;
        s1.name = QStringLiteral("四格");
        s1.zones << ZoneSpec{ QRectF(margin, margin, zw, zh), QStringLiteral("全景片") }
                 << ZoneSpec{ QRectF(margin + zw + gap, margin, zw, zh), QStringLiteral("正面照") }
                 << ZoneSpec{ QRectF(margin, margin + zh + gap, zw, zh), QStringLiteral("上颌") }
                 << ZoneSpec{ QRectF(margin + zw + gap, margin + zh + gap, zw, zh), QStringLiteral("下颌") };
        list << s1;

        LayoutSpec s2;
        s2.name = QStringLiteral("三格·上下颌分开");
        s2.zones << ZoneSpec{ QRectF(margin, margin, ww, zh), QStringLiteral("正面照") }
                 << ZoneSpec{ QRectF(margin, margin + zh + gap, zw, zh), QStringLiteral("上颌") }
                 << ZoneSpec{ QRectF(margin + zw + gap, margin + zh + gap, zw, zh), QStringLiteral("下颌") };
        list << s2;

        LayoutSpec s3;
        s3.name = QStringLiteral("三格·上下颌合体");
        s3.zones << ZoneSpec{ QRectF(margin, margin, zw, zh), QStringLiteral("全景片") }
                 << ZoneSpec{ QRectF(margin + zw + gap, margin, zw, zh), QStringLiteral("正面照") }
                 << ZoneSpec{ QRectF(margin, margin + zh + gap, ww, zh), QStringLiteral("上下颌") };
        list << s3;

        return list;
    }();
    return templates;
}

void CanvasView::buildScene()
{
    m_scene.setSceneRect(0, 0, 1600.0, 1100.0);
    applyLayout(0);
}

int CanvasView::layoutCount() const
{
    return layoutTemplates().size();
}

QString CanvasView::layoutName(int index) const
{
    const QList<LayoutSpec> &templates = layoutTemplates();
    if (index >= 0 && index < templates.size())
        return templates.at(index).name;
    return QString();
}

void CanvasView::applyLayout(int index)
{
    const QList<LayoutSpec> &templates = layoutTemplates();
    if (index < 0 || index >= templates.size())
        return;

    clearContent(); // 移除已有图片与文字

    for (PhotoZone *zone : m_zones) {
        if (zone->labelItem()) {
            m_scene.removeItem(zone->labelItem());
            delete zone->labelItem();
        }
        m_scene.removeItem(zone);
        delete zone;
    }
    m_zones.clear();

    const LayoutSpec &spec = templates.at(index);
    for (const ZoneSpec &zs : spec.zones) {
        auto *zone = new PhotoZone(zs.rect, zs.label, nullptr);
        m_scene.addItem(zone);

        auto *labelItem = m_scene.addSimpleText(zs.label);
        QFont f = labelItem->font();
        f.setPixelSize(20);
        labelItem->setFont(f);
        labelItem->setBrush(kLabelText);
        labelItem->setPos(zs.rect.topLeft() + QPointF(12.0, 8.0));
        labelItem->setZValue(10);
        zone->setLabelItem(labelItem);

        m_zones.append(zone);
    }

    m_layoutIndex = index;
    fitScene();
}

bool CanvasView::isImageFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QStringLiteral("png") || s == QStringLiteral("jpg") || s == QStringLiteral("jpeg")
        || s == QStringLiteral("bmp") || s == QStringLiteral("gif")
        || s == QStringLiteral("tif") || s == QStringLiteral("tiff");
}

PhotoZone *CanvasView::zoneAt(const QPointF &scenePos) const
{
    for (PhotoZone *zone : m_zones) {
        if (zone->contains(scenePos))
            return zone;
    }
    return nullptr;
}

PhotoZone *CanvasView::firstEmptyZone() const
{
    for (PhotoZone *zone : m_zones) {
        if (!zone->hasImage())
            return zone;
    }
    return nullptr;
}

bool CanvasView::importInto(PhotoZone *zone, const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        emit statusMessage(tr("无法读取图片：%1").arg(QFileInfo(path).fileName()));
        return false;
    }
    zone->setImage(image, path);
    return true;
}

void CanvasView::importImages(const QStringList &files)
{
    int count = 0;
    for (const QString &file : files) {
        PhotoZone *zone = firstEmptyZone();
        if (!zone) {
            emit statusMessage(tr("分区已满，剩余图片已忽略"));
            break;
        }
        if (importInto(zone, file))
            ++count;
    }
    if (count > 0)
        emit statusMessage(tr("已导入 %1 张图片").arg(count));
}

TextItem *CanvasView::createTextItem(const QString &text)
{
    auto *item = new TextItem(text);
    m_scene.addItem(item);
    item->setZValue(20);
    QFont f = item->font();
    f.setPixelSize(m_textPixelSize);
    item->setFont(f);
    item->setDefaultTextColor(m_textColor);
    item->setTextWidth(-1);
    item->setFlag(QGraphicsItem::ItemIsMovable, true);
    item->setFlag(QGraphicsItem::ItemIsSelectable, true);
    item->setFlag(QGraphicsItem::ItemIsFocusable, true);
    return item;
}

void CanvasView::addText(const QString &text)
{
    TextItem *item = createTextItem(text);
    int count = 0;
    const QList<QGraphicsItem *> items = m_scene.items();
    for (QGraphicsItem *it : items) {
        if (dynamic_cast<TextItem *>(it))
            ++count;
    }
    const QRectF br = item->boundingRect();
    item->setPos((m_scene.width() - br.width()) / 2.0,
                 (m_scene.height() - br.height()) / 2.0 + (count - 1) * 40.0);
}

void CanvasView::setTextStyle(int pixelSize, const QColor &color)
{
    m_textPixelSize = pixelSize;
    m_textColor = color;
}

void CanvasView::applyStyleToSelection(int pixelSize, const QColor &color)
{
    bool applied = false;
    const QList<QGraphicsItem *> selected = m_scene.selectedItems();
    for (QGraphicsItem *item : selected) {
        if (auto *text = dynamic_cast<TextItem *>(item)) {
            QFont f = text->font();
            f.setPixelSize(pixelSize);
            text->setFont(f);
            text->setDefaultTextColor(color);
            applied = true;
        }
    }
    if (applied)
        emit statusMessage(tr("已更新选中文字的样式"));
}

void CanvasView::setTemplateText(const QStringList &lines)
{
    for (TextItem *item : m_templateItems) {
        m_scene.removeItem(item);
        delete item;
    }
    m_templateItems.clear();
    const QColor clinicalRed(0xc0, 0x39, 0x2b);
    qreal y = 40.0;
    for (const QString &line : lines) {
        TextItem *item = createTextItem(line);
        QFont f = item->font();
        f.setPixelSize(28);
        item->setFont(f);
        item->setDefaultTextColor(clinicalRed);
        item->setPos(40.0, y);
        y += 42.0;
        m_templateItems.append(item);
    }
    emit statusMessage(tr("已生成患者信息文字"));
}

void CanvasView::removeSelected()
{
    const QList<QGraphicsItem *> selected = m_scene.selectedItems();
    if (selected.isEmpty())
        return;
    for (QGraphicsItem *item : selected) {
        if (auto *photo = dynamic_cast<PhotoItem *>(item)) {
            if (PhotoZone *zone = photo->zone())
                zone->takeImage();
        } else if (auto *text = dynamic_cast<TextItem *>(item)) {
            m_scene.removeItem(text);
            delete text;
        }
    }
    emit statusMessage(tr("已移除 %1 个元素").arg(selected.size()));
}

void CanvasView::clearContent()
{
    const QList<QGraphicsItem *> items = m_scene.items();
    for (QGraphicsItem *item : items) {
        if (auto *photo = dynamic_cast<PhotoItem *>(item)) {
            if (PhotoZone *zone = photo->zone())
                zone->takeImage();
        } else if (auto *text = dynamic_cast<TextItem *>(item)) {
            m_scene.removeItem(text);
            delete text;
        }
    }
    m_templateItems.clear();
    m_scene.clearSelection();
    emit statusMessage(tr("画布已清空"));
}

void CanvasView::requestMoveBetweenZones(PhotoItem *item, const QPointF &scenePos)
{
    QTimer::singleShot(0, this, [this, item, scenePos]() {
        PhotoZone *src = item->zone();
        if (!src || src->item() != item)
            return;
        PhotoZone *dst = zoneAt(scenePos);
        if (!dst || dst == src)
            return;
        const QString sourcePath = item->sourcePath();
        const QImage image = item->image();
        src->takeImage();
        dst->setImage(image, sourcePath);
        emit statusMessage(tr("图片已移动到“%1”").arg(dst->label()));
    });
}
void CanvasView::renderContent(QPainter *painter, const QRectF &target)
{
    // 导出/打印共用：白底、无分区装饰
    m_scene.setBackgroundBrush(Qt::white);
    for (PhotoZone *zone : m_zones) {
        zone->setVisible(false);
        if (zone->labelItem())
            zone->labelItem()->setVisible(false);
    }
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_scene.render(painter, target, m_scene.sceneRect());
    m_scene.setBackgroundBrush(kSceneBg);
    for (PhotoZone *zone : m_zones) {
        zone->setVisible(true);
        if (zone->labelItem())
            zone->labelItem()->setVisible(true);
    }
}

bool CanvasView::exportImage(const QString &path, qreal scale)
{
    if (scale <= 0.0)
        scale = 2.0;
    m_scene.clearSelection();
    const QRectF src = m_scene.sceneRect();
    const QSize size(int(src.width() * scale + 0.5), int(src.height() * scale + 0.5));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        renderContent(&painter, QRectF(0, 0, image.width(), image.height()));
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    bool ok = false;
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        ok = image.save(path, "JPEG", 92);
    else
        ok = image.save(path, "PNG");
    if (!ok)
        emit statusMessage(tr("导出失败：%1").arg(path));
    return ok;
}

bool CanvasView::exportPdf(const QString &path)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Landscape);
    QPainter painter;
    if (!painter.begin(&printer))
        return false;
    m_scene.clearSelection();
    const qreal margin = 15.0 / 25.4 * printer.resolution();
    renderContent(&painter,
        QRectF(margin, margin, printer.width() - 2 * margin, printer.height() - 2 * margin));
    painter.end();
    return true;
}

bool CanvasView::saveProject(const QString &path)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("layout"), m_layoutIndex);
    root.insert(QStringLiteral("textPixelSize"), m_textPixelSize);
    root.insert(QStringLiteral("textColor"), m_textColor.name(QColor::HexRgb));

    QJsonArray zoneArray;
    for (PhotoZone *zone : m_zones) {
        QJsonObject o;
        if (PhotoItem *item = zone->item()) {
            o.insert(QStringLiteral("image"), item->sourcePath());
            o.insert(QStringLiteral("x"), item->pos().x());
            o.insert(QStringLiteral("y"), item->pos().y());
            o.insert(QStringLiteral("scale"), item->scale());
        }
        zoneArray.append(o);
    }
    root.insert(QStringLiteral("zones"), zoneArray);

    QJsonArray textArray;
    const QList<QGraphicsItem *> items = m_scene.items();
    for (QGraphicsItem *item : items) {
        if (auto *text = dynamic_cast<TextItem *>(item)) {
            QJsonObject o;
            o.insert(QStringLiteral("text"), text->toPlainText());
            o.insert(QStringLiteral("x"), text->pos().x());
            o.insert(QStringLiteral("y"), text->pos().y());
            o.insert(QStringLiteral("pixelSize"), text->font().pixelSize());
            o.insert(QStringLiteral("color"), text->defaultTextColor().name(QColor::HexRgb));
            textArray.append(o);
        }
    }
    root.insert(QStringLiteral("texts"), textArray);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool CanvasView::loadProject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();

    applyLayout(root.value(QStringLiteral("layout")).toInt(0));
    m_textPixelSize = root.value(QStringLiteral("textPixelSize")).toInt(28);
    m_textColor = QColor(root.value(QStringLiteral("textColor")).toString(QStringLiteral("#1f2937")));

    int missing = 0;
    const QJsonArray zoneArray = root.value(QStringLiteral("zones")).toArray();
    for (int i = 0; i < zoneArray.size() && i < m_zones.size(); ++i) {
        const QJsonObject o = zoneArray.at(i).toObject();
        const QString imagePath = o.value(QStringLiteral("image")).toString();
        if (imagePath.isEmpty())
            continue;
        if (!QFileInfo::exists(imagePath)) {
            ++missing;
            continue;
        }
        QImageReader reader(imagePath);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            ++missing;
            continue;
        }
        m_zones.at(i)->setImage(image, imagePath);
        if (PhotoItem *item = m_zones.at(i)->item()) {
            item->setScale(o.value(QStringLiteral("scale")).toDouble(1.0));
            item->setPos(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
        }
    }

    const QJsonArray textArray = root.value(QStringLiteral("texts")).toArray();
    for (const QJsonValue &value : textArray) {
        const QJsonObject o = value.toObject();
        TextItem *item = createTextItem(o.value(QStringLiteral("text")).toString());
        QFont f = item->font();
        f.setPixelSize(o.value(QStringLiteral("pixelSize")).toInt(m_textPixelSize));
        item->setFont(f);
        item->setDefaultTextColor(QColor(o.value(QStringLiteral("color")).toString(QStringLiteral("#1f2937"))));
        item->setPos(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
    }

    if (missing > 0)
        emit statusMessage(tr("已打开工程，%1 张图片未找到").arg(missing));
    else
        emit statusMessage(tr("已打开工程"));
    return true;
}

// ---------------------------------------------------------------------------
// 事件处理
// ---------------------------------------------------------------------------

void CanvasView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (isImageFile(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void CanvasView::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

void CanvasView::dropEvent(QDropEvent *event)
{
    QStringList files;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && isImageFile(path))
            files << path;
    }
    if (files.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();

    int index = 0;
    int count = 0;
    const QPointF scenePos = mapToScene(QT_MOUSE_POS(event));
    if (PhotoZone *target = zoneAt(scenePos)) {
        if (importInto(target, files.at(index)))
            ++count;
        ++index;
    }
    for (; index < files.size(); ++index) {
        PhotoZone *zone = firstEmptyZone();
        if (!zone) {
            emit statusMessage(tr("分区已满，剩余图片已忽略"));
            break;
        }
        if (importInto(zone, files.at(index)))
            ++count;
    }
    emit statusMessage(tr("已导入 %1 张图片").arg(count));
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPointF scenePos = mapToScene(QT_MOUSE_POS(event));
    QGraphicsItem *item = m_scene.itemAt(scenePos, QTransform());
    if (auto *text = dynamic_cast<TextItem *>(item)) {
        text->startEdit();
        return;
    }
    if (auto *photo = dynamic_cast<PhotoItem *>(item)) {
        photo->fitToZone();
        return;
    }
    if (dynamic_cast<PhotoZone *>(item)) {
        const QString file = QFileDialog::getOpenFileName(this, tr("选择图片"), QString(),
            tr("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff)"));
        if (!file.isEmpty())
            importInto(static_cast<PhotoZone *>(item), file);
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    // 图片条目自己处理缩放；空白处不滚动。
    event->ignore();
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        if (auto *text = dynamic_cast<TextItem *>(m_scene.focusItem())) {
            if (text->textInteractionFlags() != Qt::NoTextInteraction) {
                QGraphicsView::keyPressEvent(event);
                return;
            }
        }
        removeSelected();
        return;
    }
    if (key == Qt::Key_Escape) {
        m_scene.clearSelection();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    fitScene();
}

void CanvasView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    fitScene();
}

void CanvasView::fitScene()
{
    if (m_scene.sceneRect().isEmpty())
        return;
    fitInView(m_scene.sceneRect(), Qt::KeepAspectRatio);
}
