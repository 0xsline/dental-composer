// 四区画布：分区、图片条目、文字条目与主视图。
#pragma once

#include <QColor>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

class CanvasView;
class PhotoZone;
class QGraphicsSimpleTextItem;
class QPainterPath;

// 分区内的一张图片：平移、滚轮缩放、双击适应、可拖到其他分区。
class PhotoItem : public QGraphicsPixmapItem
{
public:
    PhotoItem(const QImage &image, PhotoZone *zone);

    void fitToZone();
    QPainterPath localClipPath() const;
    PhotoZone *zone() const { return m_zone; }
    QImage image() const { return m_image; }
    int type() const override { return UserType + 1; }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void wheelEvent(QGraphicsSceneWheelEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    void clampToZone();
    qreal fitScale() const;
    qreal maxScale() const;

    QImage m_image;
    PhotoZone *m_zone = nullptr;
    QPointF m_lastScenePos;
    bool m_dragging = false;
};

// 画布上的一个分区：背景、名称标签，容纳一张图片。
class PhotoZone : public QGraphicsRectItem
{
public:
    PhotoZone(const QRectF &rect, const QString &label, QGraphicsItem *parent = nullptr);

    void setImage(const QImage &image); // 装入图片并自动适应
    QImage takeImage();                 // 取出并删除内部图片条目
    bool hasImage() const { return m_item != nullptr; }
    PhotoItem *item() const { return m_item; }
    QString label() const { return m_label; }

    QGraphicsSimpleTextItem *labelItem() const { return m_labelItem; }
    void setLabelItem(QGraphicsSimpleTextItem *item) { m_labelItem = item; }

    int type() const override { return UserType + 2; }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;

private:
    QString m_label;
    PhotoItem *m_item = nullptr;
    QGraphicsSimpleTextItem *m_labelItem = nullptr;
};

// 文字条目：双击进入编辑，失焦退出，空内容自动删除。
class TextItem : public QGraphicsTextItem
{
public:
    explicit TextItem(const QString &text, QGraphicsItem *parent = nullptr);
    void startEdit();
    int type() const override { return UserType + 3; }

protected:
    void focusOutEvent(QFocusEvent *event) override;
};

// 主画布视图：四分区、拖入图片、文字、导出。
class CanvasView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CanvasView(QWidget *parent = nullptr);

    const QList<PhotoZone *> &zones() const { return m_zones; }

    void importImages(const QStringList &files);          // 顺序装入空分区
    void addText(const QString &text);
    void setTextStyle(int pixelSize, const QColor &color);
    void removeSelected();
    void clearContent();                                   // 清空图片与文字，保留分区
    bool exportImage(const QString &path, qreal scale);    // 按后缀导出 PNG/JPEG
    PhotoZone *zoneAt(const QPointF &scenePos) const;
    void requestMoveBetweenZones(PhotoItem *item, const QPointF &scenePos);

signals:
    void statusMessage(const QString &message);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void buildScene();
    PhotoZone *firstEmptyZone() const;
    bool importInto(PhotoZone *zone, const QString &path);
    static bool isImageFile(const QString &path);
    void fitScene();

    QGraphicsScene m_scene;
    QList<PhotoZone *> m_zones;
    int m_textPixelSize = 28;
    QColor m_textColor = QColor(0x1f, 0x29, 0x37);
};
