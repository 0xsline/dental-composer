#include "canvasview.h"
#include "qtcompat.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QFocusEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QGraphicsSimpleTextItem>
#include <QImageReader>
#include <QKeyEvent>
#include <QKeySequence>
#include <QVariant>
#include <QMimeData>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>

#include <cmath>

#if defined(Q_OS_MAC)
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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

QString embedFormatForPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return QStringLiteral("JPEG");
    return QStringLiteral("PNG");
}

QByteArray encodeEmbeddedImage(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (image.hasAlphaChannel())
        image.save(&buffer, "PNG");
    else
        image.save(&buffer, "JPEG", 92);
    return bytes;
}

QImage imageFromZoneObject(const QJsonObject &o, QString *pathOut, int *rotationOut)
{
    const QString imagePath = o.value(QStringLiteral("image")).toString();
    if (pathOut)
        *pathOut = imagePath;
    int rotation = o.value(QStringLiteral("rotation")).toInt(0);

    const QString b64 = o.value(QStringLiteral("imageData")).toString();
    if (!b64.isEmpty()) {
        const QByteArray raw = QByteArray::fromBase64(b64.toLatin1());
        QImage embedded;
        if (embedded.loadFromData(raw)) {
            if (rotationOut)
                *rotationOut = rotation;
            return embedded;
        }
    }

    if (!imagePath.isEmpty() && QFileInfo::exists(imagePath)) {
        QImageReader reader(imagePath);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull()) {
            if (rotationOut)
                *rotationOut = rotation;
            return image;
        }
    }

    if (rotationOut)
        *rotationOut = 0;
    return QImage();
}

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
    setAcceptHoverEvents(true);
    setCursor(Qt::OpenHandCursor);
    setTransformationMode(Qt::SmoothTransformation);
    // 矩形图片用包围盒命中检测（免逐像素 mask）；拖动平移时用设备缓存，缩放时才重建
    setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    fitToZone();
}

QRectF PhotoItem::boardRect() const
{
    if (m_zone)
        return m_zone->rect();
    if (scene())
        return scene()->sceneRect().adjusted(28.0, 28.0, -28.0, -28.0);
    return QRectF();
}

qreal PhotoItem::containScale() const
{
    const QRectF zr = boardRect();
    if (zr.isEmpty() || pixmap().isNull())
        return 1.0;
    return qMin(zr.width() / pixmap().width(), zr.height() / pixmap().height());
}

qreal PhotoItem::coverScale() const
{
    const QRectF zr = boardRect();
    if (zr.isEmpty() || pixmap().isNull())
        return 1.0;
    return qMax(zr.width() / pixmap().width(), zr.height() / pixmap().height());
}

qreal PhotoItem::fitScale() const
{
    // 默认整图可见（contain），保持宽高比
    return containScale();
}

qreal PhotoItem::minScale() const
{
    if (pixmap().isNull())
        return 0.05;
    const qreal minPx = 48.0;
    return qMax(minPx / qreal(pixmap().width()), minPx / qreal(pixmap().height()));
}

qreal PhotoItem::maxScale() const
{
    return qMax(6.0 * coverScale(), 4.0);
}

QRectF PhotoItem::imageSceneRect() const
{
    return QRectF(pos(), QSizeF(pixmap().size()) * scale());
}

qreal PhotoItem::handlePad() const
{
    return 16.0 / qMax(scale(), 0.01);
}

QRectF PhotoItem::boundingRect() const
{
    const qreal pad = handlePad();
    return QGraphicsPixmapItem::boundingRect().adjusted(-pad, -pad, pad, pad);
}

QPainterPath PhotoItem::shape() const
{
    QPainterPath path;
    path.addRect(boundingRect());
    return path;
}

int PhotoItem::normalizeRotation(int degrees)
{
    degrees %= 360;
    if (degrees < 0)
        degrees += 360;
    return (degrees / 90) * 90;
}

void PhotoItem::refreshDisplayPixmap()
{
    if (m_image.isNull())
        return;
    QImage display = m_image;
    const QRectF zr = boardRect();
    if (!zr.isEmpty()) {
        const qreal ratio = qMin(zr.width() * 3.0 / m_image.width(),
                                 zr.height() * 3.0 / m_image.height());
        if (ratio < 1.0)
            display = m_image.scaled(QSize(int(m_image.width() * ratio), int(m_image.height() * ratio)),
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    setPixmap(QPixmap::fromImage(display));
}

void PhotoItem::applyStoredRotation(int degrees)
{
    const int target = normalizeRotation(degrees);
    const int delta = (target - m_rotation + 360) % 360;
    if (delta == 0 || m_image.isNull()) {
        m_rotation = target;
        return;
    }
    QTransform transform;
    transform.rotate(delta);
    m_image = m_image.transformed(transform, Qt::FastTransformation);
    m_rotation = target;
    refreshDisplayPixmap();
}

void PhotoItem::rotateBy90(int quarterTurns)
{
    if (m_image.isNull() || quarterTurns == 0)
        return;
    const QPointF center = imageSceneRect().center();
    applyStoredRotation(m_rotation + 90 * quarterTurns);
    const QSizeF sz = QSizeF(pixmap().size()) * scale();
    setPos(center - QPointF(sz.width() / 2.0, sz.height() / 2.0));
    clampToZone();
}

void PhotoItem::fitToZone()
{
    const QRectF zr = boardRect();
    if (zr.isEmpty() || pixmap().isNull())
        return;
    const qreal s = fitScale();
    setScale(s);
    const QSizeF sz = QSizeF(pixmap().size()) * s;
    setPos(zr.topLeft() + QPointF((zr.width() - sz.width()) / 2.0, (zr.height() - sz.height()) / 2.0));
    clampToZone();
}

void PhotoItem::placeCentered(const QPointF &sceneCenter, qreal boardFraction)
{
    const QRectF zr = boardRect();
    if (zr.isEmpty() || pixmap().isNull())
        return;
    const qreal frac = qBound(0.08, boardFraction, 1.0);
    const qreal s = qMin(containScale(),
                         qMin(zr.width() * frac / pixmap().width(),
                              zr.height() * frac / pixmap().height()));
    setScale(qBound(minScale(), s, maxScale()));
    const QSizeF sz = QSizeF(pixmap().size()) * scale();
    setPos(sceneCenter - QPointF(sz.width() / 2.0, sz.height() / 2.0));
    clampToZone();
}

void PhotoItem::clampToZone()
{
    const QRectF zr = boardRect();
    if (zr.isEmpty())
        return;
    const QSizeF sz = QSizeF(pixmap().size()) * scale();
    QPointF p = pos();
    if (sz.width() >= zr.width())
        p.setX(qBound(zr.right() - sz.width(), p.x(), zr.left()));
    else
        p.setX(qBound(zr.left(), p.x(), zr.right() - sz.width()));
    if (sz.height() >= zr.height())
        p.setY(qBound(zr.bottom() - sz.height(), p.y(), zr.top()));
    else
        p.setY(qBound(zr.top(), p.y(), zr.bottom() - sz.height()));
    setPos(p);
}


QPainterPath PhotoItem::localClipPath() const
{
    QPainterPath path;
    const QRectF zr = boardRect();
    if (!zr.isEmpty())
        path.addPolygon(mapFromParent(zr));
    return path;
}

void PhotoItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    painter->save();
    painter->setClipPath(localClipPath());
    QGraphicsPixmapItem::paint(painter, option, widget);
    painter->restore();

    // 句柄画在图片自身四角/边中点（item 坐标），不是分区边框
    if ((isSelected() || m_hovered) && !pixmap().isNull()) {
        painter->save();
        if (!boardRect().isEmpty())
            painter->setClipPath(localClipPath());
        const QRectF r = QGraphicsPixmapItem::boundingRect();
        const QPointF c[4] = { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() };
        const QPointF e[4] = {
            (c[0] + c[1]) / 2, (c[1] + c[2]) / 2, (c[2] + c[3]) / 2, (c[3] + c[0]) / 2
        };
        const qreal sz = handlePad();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(QColor(0x2b, 0x57, 0x9a), qMax(1.0, 1.8 / scale())));
        painter->setBrush(Qt::white);
        for (int i = 0; i < 4; ++i) {
            painter->drawRect(QRectF(c[i].x() - sz / 2, c[i].y() - sz / 2, sz, sz));
            painter->drawRect(QRectF(e[i].x() - sz / 2, e[i].y() - sz / 2, sz, sz));
        }
        painter->restore();
    }
}


PhotoItem::ResizeHandle PhotoItem::handleAt(const QPointF &scenePos) const
{
    if (pixmap().isNull())
        return ResizeHandle::None;
    const QRectF r = imageSceneRect();
    const qreal hit = 20.0; // 场景坐标命中半径
    static const ResizeHandle corners[4] = {
        ResizeHandle::CornerTopLeft, ResizeHandle::CornerTopRight,
        ResizeHandle::CornerBottomRight, ResizeHandle::CornerBottomLeft
    };
    static const ResizeHandle edges[4] = {
        ResizeHandle::EdgeTop, ResizeHandle::EdgeRight,
        ResizeHandle::EdgeBottom, ResizeHandle::EdgeLeft
    };
    const QPointF c[4] = { r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() };
    for (int i = 0; i < 4; ++i) {
        if (QLineF(scenePos, c[i]).length() <= hit)
            return corners[i];
    }
    for (int i = 0; i < 4; ++i) {
        const QPointF mid = (c[i] + c[(i + 1) % 4]) / 2;
        if (QLineF(scenePos, mid).length() <= hit)
            return edges[i];
    }
    return ResizeHandle::None;
}

QPointF PhotoItem::resizeAnchor(ResizeHandle handle) const
{
    const QRectF r = imageSceneRect();
    switch (handle) {
    case ResizeHandle::CornerTopLeft:
        return r.bottomRight();
    case ResizeHandle::CornerTopRight:
        return r.bottomLeft();
    case ResizeHandle::CornerBottomRight:
        return r.topLeft();
    case ResizeHandle::CornerBottomLeft:
        return r.topRight();
    case ResizeHandle::EdgeLeft:
        return QPointF(r.right(), r.center().y());
    case ResizeHandle::EdgeRight:
        return QPointF(r.left(), r.center().y());
    case ResizeHandle::EdgeTop:
        return QPointF(r.center().x(), r.bottom());
    case ResizeHandle::EdgeBottom:
        return QPointF(r.center().x(), r.top());
    default:
        return r.center();
    }
}

qreal PhotoItem::resizeFactor(ResizeHandle handle, const QPointF &mouseScene) const
{
    const QRectF r = imageSceneRect();
    const QPointF anchor = resizeAnchor(handle);
    switch (handle) {
    case ResizeHandle::EdgeLeft:
    case ResizeHandle::EdgeRight: {
        const qreal d0 = qAbs(r.center().x() - anchor.x());
        const qreal d1 = qAbs(mouseScene.x() - anchor.x());
        return d1 / qMax(d0, 1.0);
    }
    case ResizeHandle::EdgeTop:
    case ResizeHandle::EdgeBottom: {
        const qreal d0 = qAbs(r.center().y() - anchor.y());
        const qreal d1 = qAbs(mouseScene.y() - anchor.y());
        return d1 / qMax(d0, 1.0);
    }
    default: {
        QPointF corner = r.topLeft();
        if (handle == ResizeHandle::CornerTopRight)
            corner = r.topRight();
        else if (handle == ResizeHandle::CornerBottomRight)
            corner = r.bottomRight();
        else if (handle == ResizeHandle::CornerBottomLeft)
            corner = r.bottomLeft();
        const qreal d0 = QLineF(corner, anchor).length();
        const qreal d1 = QLineF(mouseScene, anchor).length();
        return d1 / qMax(d0, 1.0);
    }
    }
}

void PhotoItem::applyAnchoredZoom(qreal factor, const QPointF &anchorScene)
{
    if (pixmap().isNull())
        return;
    const qreal s0 = scale();
    if (s0 <= 0.0)
        return;
    const QPointF P0 = pos();
    const qreal s = qBound(minScale(), s0 * factor, maxScale());
    const qreal f = s / s0;
    setScale(s);
    setPos(anchorScene - (anchorScene - P0) * f);
    clampToZone();
}

void PhotoItem::applyResize(ResizeHandle handle, const QPointF &mouseScene)
{
    if (handle == ResizeHandle::None)
        return;
    applyAnchoredZoom(resizeFactor(handle, mouseScene), resizeAnchor(handle));
}

void PhotoItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    m_hovered = true;
    update();
    QGraphicsPixmapItem::hoverEnterEvent(event);
}

void PhotoItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    m_hovered = false;
    update();
    QGraphicsPixmapItem::hoverLeaveEvent(event);
}

void PhotoItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    if (handleAt(event->scenePos()) != ResizeHandle::None) {
        switch (handleAt(event->scenePos())) {
        case ResizeHandle::CornerTopLeft:
        case ResizeHandle::CornerBottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case ResizeHandle::CornerTopRight:
        case ResizeHandle::CornerBottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case ResizeHandle::EdgeLeft:
        case ResizeHandle::EdgeRight:
            setCursor(Qt::SizeHorCursor);
            break;
        case ResizeHandle::EdgeTop:
        case ResizeHandle::EdgeBottom:
            setCursor(Qt::SizeVerCursor);
            break;
        default:
            setCursor(Qt::OpenHandCursor);
            break;
        }
    } else {
        setCursor(Qt::OpenHandCursor);
    }
    QGraphicsPixmapItem::hoverMoveEvent(event);
}

void PhotoItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (CanvasView *view = viewFor(this))
            view->raisePhoto(this);
        // 点在图片句柄上 → 等比改大小；点在图内 → 平移
        const ResizeHandle handle = handleAt(event->scenePos());
        if (handle != ResizeHandle::None) {
            if (CanvasView *view = viewFor(this))
                view->pushUndoSnapshot();
            m_resizing = true;
            m_resizeHandle = handle;
            m_resizeAnchor = resizeAnchor(handle);
            setSelected(true);
            event->accept();
            return;
        }
        if (CanvasView *view = viewFor(this))
            view->pushUndoSnapshot();
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
    if (m_resizing) {
        applyResize(m_resizeHandle, event->scenePos());
        event->accept();
        return;
    }
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
    if (m_resizing) {
        m_resizing = false;
        m_resizeHandle = ResizeHandle::None;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return; // 缩放拖拽不触发跨分区移动
    }
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

void PhotoItem::zoomBy(qreal factor, const QPointF &scenePos)
{
    applyAnchoredZoom(factor, scenePos);
}

void PhotoItem::wheelEvent(QGraphicsSceneWheelEvent *event)
{
    int dy = event->delta();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (dy == 0)
        dy = event->pixelDelta().y();
#endif
    if (dy == 0) {
        event->ignore();
        return;
    }
    if (m_lastWheelUndo.elapsed() > 800) {
        if (CanvasView *view = viewFor(this))
            view->pushUndoSnapshot();
        m_lastWheelUndo.restart();
    }
    qreal notches = dy / 120.0;
    if (qAbs(notches) < 0.35)
        notches = (dy > 0) ? 0.35 : -0.35;
    notches = qBound(-2.0, notches, 2.0);
    // 绕图片中心等比例缩放，可小于分区
    zoomBy(std::pow(1.15, notches), imageSceneRect().center());
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
    bool occupied = hasImage();
    if (!occupied && scene()) {
        const QList<QGraphicsItem *> hits = scene()->items(r);
        for (QGraphicsItem *it : hits) {
            if (dynamic_cast<PhotoItem *>(it)) {
                occupied = true;
                break;
            }
        }
    }
    if (occupied) {
        painter->setPen(QPen(kZoneBorderFull, 1.0));
        painter->drawPath(path);
    } else {
        painter->setPen(QPen(kZoneBorder, 1.5, Qt::DashLine));
        painter->drawPath(path);
        QFont f = painter->font();
        f.setPixelSize(22);
        painter->setFont(f);
        painter->setPen(kZoneHint);
        painter->drawText(r, Qt::AlignCenter,
                          m_label.isEmpty() ? QStringLiteral("拖入图片，自由摆放")
                                            : QStringLiteral("拖入图片 或 双击导入"));
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
    if (CanvasView *view = viewFor(this))
        view->pushUndoSnapshot();
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
    setContextMenuPolicy(Qt::DefaultContextMenu);
    viewport()->setContextMenuPolicy(Qt::DefaultContextMenu);

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

        LayoutSpec s4;
        s4.name = QStringLiteral("自由拼图");
        s4.freeform = true;
        s4.zones << ZoneSpec{ QRectF(margin, margin, ww, H - 2 * margin), QString() };
        list << s4;

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

bool CanvasView::isFreeLayout() const
{
    const QList<LayoutSpec> &templates = layoutTemplates();
    return m_layoutIndex >= 0 && m_layoutIndex < templates.size()
        && templates.at(m_layoutIndex).freeform;
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

        if (!zs.label.isEmpty()) {
            auto *labelItem = m_scene.addSimpleText(zs.label);
            QFont f = labelItem->font();
            f.setPixelSize(20);
            labelItem->setFont(f);
            labelItem->setBrush(kLabelText);
            labelItem->setPos(zs.rect.topLeft() + QPointF(12.0, 8.0));
            labelItem->setZValue(10);
            zone->setLabelItem(labelItem);
        }

        m_zones.append(zone);
    }

    m_layoutIndex = index;
    m_topZ = 1;
    emit layoutChanged(index);
    fitScene();
}

bool CanvasView::isImageFile(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QString s = QFileInfo(path).suffix().toLower();
    if (s == QStringLiteral("png") || s == QStringLiteral("jpg") || s == QStringLiteral("jpeg")
        || s == QStringLiteral("bmp") || s == QStringLiteral("gif")
        || s == QStringLiteral("tif") || s == QStringLiteral("tiff")
        || s == QStringLiteral("heic") || s == QStringLiteral("heif")
        || s == QStringLiteral("webp"))
        return true;
    if (s.isEmpty() || path.contains(QLatin1String("/.file/")))
        return false;
    QImageReader reader(path);
    return reader.canRead();
}

static QString localPathFromUrl(const QUrl &url)
{
    QString path = url.toLocalFile();
    if (!path.isEmpty() && !path.contains(QLatin1String("/.file/")) && QFileInfo::exists(path))
        return path;

#if defined(Q_OS_MAC)
    const QByteArray enc = url.toEncoded();
    CFURLRef cfUrl = CFURLCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8 *>(enc.constData()),
        enc.size(), kCFStringEncodingUTF8, nullptr);
    if (cfUrl) {
        char buf[PATH_MAX];
        if (CFURLGetFileSystemRepresentation(cfUrl, true,
                reinterpret_cast<UInt8 *>(buf), sizeof(buf))) {
            const QString resolved = QString::fromUtf8(buf);
            if (!resolved.isEmpty())
                path = resolved;
        }
        CFRelease(cfUrl);
    }
#endif

    if (path.isEmpty() && url.scheme() == QLatin1String("file"))
        path = url.path();
    return path;
}

#if defined(Q_OS_MAC)
static QImage qImageFromCGImage(CGImageRef cg)
{
    if (!cg)
        return QImage();
    const size_t w = CGImageGetWidth(cg);
    const size_t h = CGImageGetHeight(cg);
    if (w == 0 || h == 0)
        return QImage();
    QImage image(int(w), int(h), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(image.bits(), w, h, 8, image.bytesPerLine(), space,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(space);
    if (!ctx)
        return QImage();
    CGContextDrawImage(ctx, CGRectMake(0, 0, CGFloat(w), CGFloat(h)), cg);
    CGContextRelease(ctx);
    return image;
}

static QImage loadWithImageIO(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return QImage();
    CFDataRef data = CFDataCreate(kCFAllocatorDefault,
        reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size());
    if (!data)
        return QImage();
    CGImageSourceRef src = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);
    if (!src)
        return QImage();
    CFMutableDictionaryRef opts = CFDictionaryCreateMutable(kCFAllocatorDefault, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(opts, kCGImageSourceCreateThumbnailFromImageAlways, kCFBooleanTrue);
    CFDictionarySetValue(opts, kCGImageSourceCreateThumbnailWithTransform, kCFBooleanTrue);
    CGImageRef cg = CGImageSourceCreateThumbnailAtIndex(src, 0, opts);
    CFRelease(opts);
    CFRelease(src);
    const QImage image = qImageFromCGImage(cg);
    if (cg)
        CGImageRelease(cg);
    return image;
}
#endif

QImage CanvasView::loadImageFile(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (!image.isNull())
        return image;
#if defined(Q_OS_MAC)
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
        image = loadWithImageIO(file.readAll());
#endif
    return image;
}

bool CanvasView::canAcceptImageDrop(const QMimeData *mime)
{
    if (!mime)
        return false;
    if (mime->hasImage())
        return true;
    if (mime->hasUrls()) {
        const QList<QUrl> urls = mime->urls();
        if (urls.isEmpty())
            return true; // Finder 可能在 enter 时还没解析出路径
        for (const QUrl &url : urls) {
            if (url.isLocalFile() || url.scheme() == QLatin1String("file"))
                return true;
            const QString path = localPathFromUrl(url);
            if (isImageFile(path))
                return true;
        }
    }
    const QStringList formats = mime->formats();
    for (const QString &fmt : formats) {
        const QString lower = fmt.toLower();
        if (lower.contains(QLatin1String("uri-list"))
            || lower.contains(QLatin1String("file-url"))
            || lower.contains(QLatin1String("filename"))
            || lower.contains(QLatin1String("file-name")))
            return true;
    }
    return false;
}

QStringList CanvasView::imagePathsFromMime(const QMimeData *mime)
{
    QStringList out;
    if (!mime)
        return out;

    QList<QUrl> urls = mime->urls();
    if (urls.isEmpty() && mime->hasFormat(QStringLiteral("text/uri-list"))) {
        const QByteArray data = mime->data(QStringLiteral("text/uri-list"));
        const QList<QByteArray> lines = data.split('\n');
        for (const QByteArray &line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('#'))
                continue;
            urls.append(QUrl::fromEncoded(trimmed));
        }
    }

    for (const QUrl &url : urls) {
        const QString path = localPathFromUrl(url);
        if (path.isEmpty())
            continue;
        if (isImageFile(path) || QImageReader(path).canRead() || QFileInfo::exists(path))
            out << path;
    }
    return out;
}

QList<DroppedImage> CanvasView::imagesFromMime(const QMimeData *mime)
{
    QList<DroppedImage> out;
    if (!mime)
        return out;

    const QStringList files = imagePathsFromMime(mime);
    for (const QString &path : files) {
        const QImage image = loadImageFile(path);
        if (!image.isNull())
            out.append(DroppedImage{ image, path });
    }
    if (!out.isEmpty())
        return out;

    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull())
            out.append(DroppedImage{ image, QString() });
    }
    if (!out.isEmpty())
        return out;

    const char *rawFmts[] = { "image/png", "image/jpeg", "image/jpg", "image/heic", "image/heif", "image/webp" };
    for (const char *fmt : rawFmts) {
        if (!mime->hasFormat(QLatin1String(fmt)))
            continue;
        const QByteArray bytes = mime->data(QLatin1String(fmt));
        QImage image;
        image.loadFromData(bytes);
#if defined(Q_OS_MAC)
        if (image.isNull())
            image = loadWithImageIO(bytes);
#endif
        if (!image.isNull())
            out.append(DroppedImage{ image, QString() });
    }
    return out;
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
    const QImage image = loadImageFile(path);
    if (image.isNull()) {
        emit statusMessage(tr("无法读取图片：%1").arg(QFileInfo(path).fileName()));
        return false;
    }
    pushUndoSnapshot();
    zone->setImage(image, path);
    return true;
}

void CanvasView::importImages(const QStringList &files)
{
    int count = 0;
    if (isFreeLayout()) {
        PhotoZone *board = m_zones.isEmpty() ? nullptr : m_zones.first();
        const QRectF boardRect = board ? board->rect() : m_scene.sceneRect().adjusted(28, 28, -28, -28);
        QPointF origin = boardRect.center();
        int offset = 0;
        for (const QString &file : files) {
            const QImage image = loadImageFile(file);
            if (image.isNull()) {
                emit statusMessage(tr("无法读取图片：%1").arg(QFileInfo(file).fileName()));
                continue;
            }
            const QPointF at = origin + QPointF(offset * 36.0, offset * 36.0);
            if (addFreeImage(image, file, at))
                ++count;
            ++offset;
        }
    } else {
        for (const QString &file : files) {
            PhotoZone *zone = firstEmptyZone();
            if (!zone) {
                emit statusMessage(tr("分区已满，剩余图片已忽略"));
                break;
            }
            if (importInto(zone, file))
                ++count;
        }
    }
    if (count > 0)
        emit statusMessage(tr("已导入 %1 张图片").arg(count));
}

PhotoItem *CanvasView::addFreeImage(const QImage &image, const QString &path, const QPointF &sceneCenter)
{
    if (image.isNull())
        return nullptr;
    PhotoZone *board = m_zones.isEmpty() ? nullptr : m_zones.first();
    QImage display = image;
    const QRectF zr = board ? board->rect() : m_scene.sceneRect().adjusted(28, 28, -28, -28);
    if (!zr.isEmpty()) {
        const qreal ratio = qMin(zr.width() * 3.0 / image.width(), zr.height() * 3.0 / image.height());
        if (ratio < 1.0)
            display = image.scaled(QSize(int(image.width() * ratio), int(image.height() * ratio)),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    pushUndoSnapshot();
    auto *item = new PhotoItem(image, display, board);
    item->setSourcePath(path);
    item->setZValue(++m_topZ);
    m_scene.addItem(item);
    item->placeCentered(sceneCenter, 0.42);
    m_looseItems.append(item);
    return item;
}

void CanvasView::raisePhoto(PhotoItem *item)
{
    if (!item)
        return;
    item->setZValue(++m_topZ);
}

void CanvasView::removeLoosePhoto(PhotoItem *photo)
{
    if (!photo)
        return;
    m_looseItems.removeAll(photo);
    if (photo->scene())
        photo->scene()->removeItem(photo);
    delete photo;
}

TextItem *CanvasView::createTextItem(const QString &text)
{
    auto *item = new TextItem(text);
    m_scene.addItem(item);
    item->setZValue(500);
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
    pushUndoSnapshot();
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
    if (!selected.isEmpty())
        pushUndoSnapshot();
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
    pushUndoSnapshot();
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
    pushUndoSnapshot();
    for (QGraphicsItem *item : selected) {
        if (auto *photo = dynamic_cast<PhotoItem *>(item)) {
            if (m_looseItems.contains(photo))
                removeLoosePhoto(photo);
            else if (PhotoZone *zone = photo->zone())
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
    pushUndoSnapshot();
    for (PhotoZone *zone : m_zones) {
        if (zone->item())
            zone->takeImage();
    }
    const QList<PhotoItem *> loose = m_looseItems;
    m_looseItems.clear();
    for (PhotoItem *photo : loose) {
        if (photo->scene())
            m_scene.removeItem(photo);
        delete photo;
    }
    const QList<QGraphicsItem *> items = m_scene.items();
    for (QGraphicsItem *item : items) {
        if (auto *text = dynamic_cast<TextItem *>(item)) {
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
        if (isFreeLayout())
            return;
        PhotoZone *src = item->zone();
        if (!src || src->item() != item)
            return;
        PhotoZone *dst = zoneAt(scenePos);
        if (!dst || dst == src)
            return;
        const QString sourcePath = item->sourcePath();
        const QImage image = item->image();
        const int rotation = item->rotationDegrees();
        src->takeImage();
        dst->setImage(image, sourcePath);
        if (PhotoItem *moved = dst->item())
            moved->setRotationField(rotation);
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

QImage CanvasView::renderToImage(qreal scale)
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
    return image;
}

bool CanvasView::exportImage(const QString &path, qreal scale)
{
    const QImage image = renderToImage(scale);
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

bool CanvasView::copyImageToClipboard(qreal scale)
{
    const QImage image = renderToImage(scale);
    if (image.isNull())
        return false;
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    clipboard->setImage(image);
    emit statusMessage(tr("已复制拼图，可直接粘贴到微信"));
    return true;
}

void CanvasView::rotateSelected(int quarterTurns)
{
    if (quarterTurns == 0)
        return;
    QList<PhotoItem *> photos;
    const QList<QGraphicsItem *> selected = m_scene.selectedItems();
    for (QGraphicsItem *item : selected) {
        if (auto *photo = dynamic_cast<PhotoItem *>(item))
            photos.append(photo);
    }
    if (photos.isEmpty()) {
        emit statusMessage(tr("请先点选一张图片再旋转"));
        return;
    }
    pushUndoSnapshot();
    for (PhotoItem *photo : photos)
        photo->rotateBy90(quarterTurns);
    emit statusMessage(tr("已旋转选中图片"));
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

QJsonObject CanvasView::encodePhoto(PhotoItem *item, bool embedImages) const
{
    QJsonObject o;
    if (!item)
        return o;
    const QString path = item->sourcePath();
    o.insert(QStringLiteral("image"), path);
    o.insert(QStringLiteral("x"), item->pos().x());
    o.insert(QStringLiteral("y"), item->pos().y());
    o.insert(QStringLiteral("scale"), item->scale());
    int rotation = item->rotationDegrees();
    const bool fileOk = !path.isEmpty() && QFileInfo::exists(path);
    const bool needEmbed = embedImages || !fileOk;
    if (needEmbed) {
        QByteArray raw;
        QString fmt = embedFormatForPath(path);
        if (embedImages && fileOk) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                raw = file.readAll();
                QImage probe;
                if (raw.isEmpty() || !probe.loadFromData(raw))
                    raw.clear();
            }
        }
        if (raw.isEmpty()) {
            raw = encodeEmbeddedImage(item->image());
            fmt = item->image().hasAlphaChannel() ? QStringLiteral("PNG")
                                                  : QStringLiteral("JPEG");
            rotation = 0;
        }
        o.insert(QStringLiteral("imageFormat"), fmt);
        o.insert(QStringLiteral("imageData"), QString::fromLatin1(raw.toBase64()));
    }
    o.insert(QStringLiteral("rotation"), rotation);
    return o;
}

PhotoItem *CanvasView::decodePhoto(const QJsonObject &o, PhotoZone *board, int *missingOut)
{
    if (o.isEmpty() || (!o.contains(QStringLiteral("image")) && !o.contains(QStringLiteral("imageData"))))
        return nullptr;
    QString imagePath;
    int rotation = 0;
    const QImage image = imageFromZoneObject(o, &imagePath, &rotation);
    if (image.isNull()) {
        if (missingOut && (!imagePath.isEmpty() || o.contains(QStringLiteral("imageData"))))
            ++(*missingOut);
        return nullptr;
    }
    PhotoItem *item = nullptr;
    if (isFreeLayout()) {
        item = addFreeImage(image, imagePath, QPointF(o.value(QStringLiteral("x")).toDouble(),
                                                      o.value(QStringLiteral("y")).toDouble()));
        if (item) {
            item->applyStoredRotation(rotation);
            item->setScale(o.value(QStringLiteral("scale")).toDouble(1.0));
            item->setPos(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
        }
    } else if (board) {
        board->setImage(image, imagePath);
        item = board->item();
        if (item) {
            item->applyStoredRotation(rotation);
            item->setScale(o.value(QStringLiteral("scale")).toDouble(1.0));
            item->setPos(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
        }
    }
    return item;
}

QByteArray CanvasView::serializeScene(bool embedImages) const
{
    QJsonObject root;
    const int version = isFreeLayout() ? 3 : (embedImages ? 2 : 1);
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("layout"), m_layoutIndex);
    root.insert(QStringLiteral("textPixelSize"), m_textPixelSize);
    root.insert(QStringLiteral("textColor"), m_textColor.name(QColor::HexRgb));

    QJsonArray zoneArray;
    for (PhotoZone *zone : m_zones) {
        if (isFreeLayout())
            zoneArray.append(QJsonObject());
        else if (PhotoItem *item = zone->item())
            zoneArray.append(encodePhoto(item, embedImages));
        else
            zoneArray.append(QJsonObject());
    }
    root.insert(QStringLiteral("zones"), zoneArray);

    if (isFreeLayout()) {
        QJsonArray photoArray;
        for (PhotoItem *item : m_looseItems)
            photoArray.append(encodePhoto(item, embedImages));
        root.insert(QStringLiteral("photos"), photoArray);
    }

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
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool CanvasView::deserializeScene(const QByteArray &data, int *missingOut)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();

    // 恢复期间暂停撤销快照
    m_undoSuspended = true;
    applyLayout(root.value(QStringLiteral("layout")).toInt(0));
    m_textPixelSize = root.value(QStringLiteral("textPixelSize")).toInt(28);
    m_textColor = QColor(root.value(QStringLiteral("textColor")).toString(QStringLiteral("#1f2937")));

    int missing = 0;
    if (isFreeLayout()) {
        const QJsonArray photoArray = root.value(QStringLiteral("photos")).toArray();
        PhotoZone *board = m_zones.isEmpty() ? nullptr : m_zones.first();
        for (const QJsonValue &value : photoArray)
            decodePhoto(value.toObject(), board, &missing);
    } else {
        const QJsonArray zoneArray = root.value(QStringLiteral("zones")).toArray();
        for (int i = 0; i < zoneArray.size() && i < m_zones.size(); ++i)
            decodePhoto(zoneArray.at(i).toObject(), m_zones.at(i), &missing);
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

    if (missingOut)
        *missingOut = missing;
    m_undoSuspended = false;
    return true;
}

bool CanvasView::saveProject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(serializeScene(true));
    return true;
}

bool CanvasView::loadProject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    int missing = 0;
    if (!deserializeScene(data, &missing))
        return false;
    m_undoStack.clear();
    if (missing > 0)
        emit statusMessage(tr("已打开工程，%1 张图片未找到").arg(missing));
    else
        emit statusMessage(tr("已打开工程"));
    return true;
}

void CanvasView::pushUndoSnapshot()
{
    if (m_undoSuspended)
        return;
    m_undoStack.append(serializeScene());
    if (m_undoStack.size() > 50)
        m_undoStack.removeFirst();
}

void CanvasView::undo()
{
    if (m_undoStack.isEmpty()) {
        emit statusMessage(tr("没有可撤销的操作"));
        return;
    }
    deserializeScene(m_undoStack.takeLast());
    emit statusMessage(tr("已撤销"));
}

// ---------------------------------------------------------------------------
// 事件处理
// ---------------------------------------------------------------------------

bool CanvasView::viewportEvent(QEvent *event)
{
    // QGraphicsView 默认把拖放交给 scene，空 scene 会 ignore，Finder 拖入永远进不来。
    switch (event->type()) {
    case QEvent::DragEnter:
        dragEnterEvent(static_cast<QDragEnterEvent *>(event));
        return true;
    case QEvent::DragMove:
        dragMoveEvent(static_cast<QDragMoveEvent *>(event));
        return true;
    case QEvent::DragLeave:
        event->accept();
        return true;
    case QEvent::Drop:
        dropEvent(static_cast<QDropEvent *>(event));
        return true;
    case QEvent::ContextMenu:
        contextMenuEvent(static_cast<QContextMenuEvent *>(event));
        return true;
    default:
        return QGraphicsView::viewportEvent(event);
    }
}

void CanvasView::dragEnterEvent(QDragEnterEvent *event)
{
    if (canAcceptImageDrop(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void CanvasView::dragMoveEvent(QDragMoveEvent *event)
{
    if (canAcceptImageDrop(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void CanvasView::dropEvent(QDropEvent *event)
{
    const QList<DroppedImage> dropped = imagesFromMime(event->mimeData());
    if (dropped.isEmpty()) {
        event->ignore();
        emit statusMessage(tr("无法读取拖入的图片（请用 Finder 拖 jpg/png，或从微信先保存再拖）"));
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->acceptProposedAction();
    placeDroppedImages(dropped, mapToScene(QT_MOUSE_POS(event)));
}

int CanvasView::placeDroppedImages(const QList<DroppedImage> &dropped, const QPointF &scenePos)
{
    int count = 0;
    QPointF pos = scenePos;
    if (pos.isNull()) {
        PhotoZone *board = m_zones.isEmpty() ? nullptr : m_zones.first();
        pos = board ? board->rect().center() : m_scene.sceneRect().center();
    }
    if (isFreeLayout()) {
        int offset = 0;
        for (const DroppedImage &item : dropped) {
            if (addFreeImage(item.image, item.path, pos + QPointF(offset * 36.0, offset * 36.0)))
                ++count;
            ++offset;
        }
    } else {
        int index = 0;
        if (PhotoZone *target = zoneAt(pos)) {
            pushUndoSnapshot();
            target->setImage(dropped.at(index).image, dropped.at(index).path);
            ++count;
            ++index;
        }
        for (; index < dropped.size(); ++index) {
            PhotoZone *zone = firstEmptyZone();
            if (!zone) {
                emit statusMessage(tr("分区已满，剩余图片已忽略"));
                break;
            }
            pushUndoSnapshot();
            zone->setImage(dropped.at(index).image, dropped.at(index).path);
            ++count;
        }
    }
    if (count > 0)
        emit statusMessage(tr("已导入 %1 张图片").arg(count));
    return count;
}

bool CanvasView::isEditingText() const
{
    if (auto *text = dynamic_cast<TextItem *>(m_scene.focusItem()))
        return text->textInteractionFlags() != Qt::NoTextInteraction;
    return false;
}

bool CanvasView::pasteFromClipboard(const QPointF &scenePos)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        emit statusMessage(tr("无法访问剪贴板"));
        return false;
    }
    QList<DroppedImage> dropped = imagesFromMime(clipboard->mimeData());
    if (dropped.isEmpty()) {
        const QImage image = clipboard->image();
        if (!image.isNull())
            dropped.append(DroppedImage{ image, QString() });
    }
    if (dropped.isEmpty()) {
        emit statusMessage(tr("剪贴板里没有图片，请先复制一张图"));
        return false;
    }
    return placeDroppedImages(dropped, scenePos) > 0;
}

void CanvasView::contextMenuEvent(QContextMenuEvent *event)
{
    const QPointF scenePos = mapToScene(event->pos());
    QGraphicsItem *hit = m_scene.itemAt(scenePos, transform());
    if (auto *photo = dynamic_cast<PhotoItem *>(hit)) {
        if (!photo->isSelected()) {
            m_scene.clearSelection();
            photo->setSelected(true);
        }
    } else if (auto *text = dynamic_cast<TextItem *>(hit)) {
        if (!text->isSelected()) {
            m_scene.clearSelection();
            text->setSelected(true);
        }
    } else if (PhotoZone *zone = zoneAt(scenePos)) {
        if (zone->item() && !zone->item()->isSelected()) {
            m_scene.clearSelection();
            zone->item()->setSelected(true);
        }
    }

    QMenu menu(this);
    QAction *actPaste = menu.addAction(tr("粘贴图片"));
    QAction *actDelete = nullptr;
    if (!m_scene.selectedItems().isEmpty())
        actDelete = menu.addAction(tr("删除"));
    const QAction *chosen = menu.exec(event->globalPos());
    if (chosen == actPaste)
        pasteFromClipboard(scenePos);
    else if (chosen == actDelete)
        removeSelected();
    event->accept();
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
            tr("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.heic *.heif *.webp)"));
        if (file.isEmpty())
            return;
        if (isFreeLayout()) {
            const QImage image = loadImageFile(file);
            if (!image.isNull())
                addFreeImage(image, file, scenePos);
        } else {
            importInto(static_cast<PhotoZone *>(item), file);
        }
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    // 交给场景分发：图片条目收到后自行缩放；空白处无滚动条、无可见效果
    QGraphicsView::wheelEvent(event);
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (event->matches(QKeySequence::Paste) && !isEditingText()) {
        pasteFromClipboard();
        return;
    }
    if (event->matches(QKeySequence::Undo)
        || (event->modifiers() & Qt::ControlModifier && key == Qt::Key_Z)
        || (event->modifiers() & Qt::MetaModifier && key == Qt::Key_Z)) {
        undo();
        return;
    }
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
