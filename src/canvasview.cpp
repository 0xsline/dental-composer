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

#include <cmath>

namespace {

CanvasView *viewFor(QGraphicsItem *item)
{
    if (!item || !item->scene() || item->scene()->views().isEmpty())
        return nullptr;
    return qobject_cast<CanvasView *>(item->scene()->views().constFirst());
}

} // namespace

// ---------------------------------------------------------------------------
// PhotoItem
// ---------------------------------------------------------------------------

PhotoItem::PhotoItem(const QImage &image, PhotoZone *zone)
    : QGraphicsPixmapItem(QPixmap::fromImage(image))
    , m_image(image)
    , m_zone(zone)
{
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setCursor(Qt::OpenHandCursor);
    setTransformationMode(Qt::SmoothTransformation);
    fitToZone();
}

qreal PhotoItem::fitScale() const
{
    if (!m_zone || m_image.isNull())
        return 1.0;
    const QRectF zr = m_zone->rect();
    return qMin(zr.width() / m_image.width(), zr.height() / m_image.height());
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
    const QSizeF sz = QSizeF(m_image.size()) * s;
    // PhotoItem 是顶层条目，坐标为场景坐标，须加上分区原点
    setPos(zr.topLeft() + QPointF((zr.width() - sz.width()) / 2.0, (zr.height() - sz.height()) / 2.0));
    clampToZone();
}

void PhotoItem::clampToZone()
{
    if (!m_zone)
        return;
    const QRectF zr = m_zone->rect();
    const QSizeF sz = QSizeF(m_image.size()) * scale();
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

void PhotoZone::setImage(const QImage &image)
{
    takeImage();
    m_item = new PhotoItem(image, this);
    m_item->setZValue(0);
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
    painter->fillPath(path, QColor(0xf7, 0xf8, 0xfa));
    if (hasImage()) {
        painter->setPen(QPen(QColor(0xd7, 0xdc, 0xe3), 1.0));
        painter->drawPath(path);
    } else {
        painter->setPen(QPen(QColor(0xcb, 0xd2, 0xdc), 1.5, Qt::DashLine));
        painter->drawPath(path);
        QFont f = painter->font();
        f.setPixelSize(22);
        painter->setFont(f);
        painter->setPen(QColor(0x9a, 0xa4, 0xb0));
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

    setBackgroundBrush(QColor(0xe8, 0xea, 0xee));
    m_scene.setBackgroundBrush(Qt::white);
}

void CanvasView::buildScene()
{
    const qreal W = 1600.0;
    const qreal H = 1100.0;
    const qreal margin = 28.0;
    const qreal gap = 20.0;
    const qreal zw = (W - 2 * margin - gap) / 2.0;
    const qreal zh = (H - 2 * margin - gap) / 2.0;

    QStringList labels;
    labels << QStringLiteral("全景片") << QStringLiteral("正面照")
           << QStringLiteral("上颌") << QStringLiteral("下颌");

    m_scene.setSceneRect(0, 0, W, H);
    for (int i = 0; i < 4; ++i) {
        const int row = i / 2;
        const int col = i % 2;
        const QRectF rect(margin + col * (zw + gap), margin + row * (zh + gap), zw, zh);

        auto *zone = new PhotoZone(rect, labels.value(i), nullptr);
        m_scene.addItem(zone);

        auto *labelItem = m_scene.addSimpleText(labels.value(i));
        QFont f = labelItem->font();
        f.setPixelSize(20);
        labelItem->setFont(f);
        labelItem->setBrush(QColor(0x4b, 0x55, 0x63));
        labelItem->setPos(rect.topLeft() + QPointF(12.0, 8.0));
        labelItem->setZValue(10);
        zone->setLabelItem(labelItem);

        m_zones.append(zone);
    }
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
    zone->setImage(image);
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

void CanvasView::addText(const QString &text)
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
        const QImage image = item->image();
        src->takeImage();
        dst->setImage(image);
        emit statusMessage(tr("图片已移动到“%1”").arg(dst->label()));
    });
}

bool CanvasView::exportImage(const QString &path, qreal scale)
{
    if (scale <= 0.0)
        scale = 2.0;
    m_scene.clearSelection();

    for (PhotoZone *zone : m_zones) {
        zone->setVisible(false);
        if (zone->labelItem())
            zone->labelItem()->setVisible(false);
    }

    const QRectF src = m_scene.sceneRect();
    const QSize size(int(src.width() * scale + 0.5), int(src.height() * scale + 0.5));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        m_scene.render(&painter, QRectF(0, 0, image.width(), image.height()), src);
    }

    for (PhotoZone *zone : m_zones) {
        zone->setVisible(true);
        if (zone->labelItem())
            zone->labelItem()->setVisible(true);
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
