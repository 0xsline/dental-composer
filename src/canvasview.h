// 四区画布：分区、图片条目、文字条目与主视图。
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QStringList>

class CanvasView;
class PhotoZone;
class QGraphicsSimpleTextItem;
class QMimeData;
class QPainterPath;

// 分区定义：位置 + 名称
struct ZoneSpec
{
    QRectF rect;
    QString label;
};

// 布局模板：名称 + 一组分区
struct LayoutSpec
{
    QString name;
    QList<ZoneSpec> zones;
};

// 分区内的一张图片：整图等比例缩放，可小于分区；拖边改图片大小，滚轮等比缩放。
class PhotoItem : public QGraphicsPixmapItem
{
public:
    // 拖边缩放句柄：在图片四角 + 四边中点（不是分区边框）
    enum class ResizeHandle {
        None,
        CornerTopLeft, CornerTopRight, CornerBottomRight, CornerBottomLeft,
        EdgeTop, EdgeRight, EdgeBottom, EdgeLeft
    };

    PhotoItem(const QImage &original, const QImage &display, PhotoZone *zone);

    void fitToZone();
    void zoomBy(qreal factor, const QPointF &scenePos);           // 以场景点为原点等比例缩放
    void applyResize(ResizeHandle handle, const QPointF &mouseScene); // 以图片对边/对角为锚点等比缩放
    QPainterPath localClipPath() const;
    QRectF imageSceneRect() const;
    PhotoZone *zone() const { return m_zone; }
    QImage image() const { return m_image; }
    QString sourcePath() const { return m_sourcePath; }
    void setSourcePath(const QString &path) { m_sourcePath = path; }
    int type() const override { return UserType + 1; }
    QRectF boundingRect() const override;
    QPainterPath shape() const override;

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void wheelEvent(QGraphicsSceneWheelEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    void clampToZone();
    qreal containScale() const;
    qreal coverScale() const;
    qreal fitScale() const;
    qreal minScale() const;
    void applyAnchoredZoom(qreal factor, const QPointF &anchorScene);
    qreal maxScale() const;
    qreal handlePad() const;
    ResizeHandle handleAt(const QPointF &scenePos) const;
    QPointF resizeAnchor(ResizeHandle handle) const;
    qreal resizeFactor(ResizeHandle handle, const QPointF &mouseScene) const;

    QImage m_image;       // 原图（导出/移动时用）
    QString m_sourcePath; // 源文件路径（工程保存用）
    PhotoZone *m_zone = nullptr;
    QPointF m_lastScenePos;
    QPointF m_resizeAnchor;
    ResizeHandle m_resizeHandle = ResizeHandle::None;
    QElapsedTimer m_lastWheelUndo;
    bool m_dragging = false;
    bool m_resizing = false;
    bool m_hovered = false;
};

// 画布上的一个分区：背景、名称标签，容纳一张图片。
class PhotoZone : public QGraphicsRectItem
{
public:
    PhotoZone(const QRectF &rect, const QString &label, QGraphicsItem *parent = nullptr);

    void setImage(const QImage &image, const QString &sourcePath = QString()); // 装入图片并自动适应（显示副本降采样，原图保留）
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

// 主画布视图：多布局分区、拖入图片、文字、导出、工程文件、撤销。
class CanvasView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CanvasView(QWidget *parent = nullptr);

    const QList<PhotoZone *> &zones() const { return m_zones; }

    int layoutCount() const;
    QString layoutName(int index) const;
    int currentLayout() const { return m_layoutIndex; }
    void applyLayout(int index); // 切换布局模板（清空现有内容）

    void importImages(const QStringList &files);          // 顺序装入空分区
    void addText(const QString &text);
    void setTextStyle(int pixelSize, const QColor &color);
    void applyStyleToSelection(int pixelSize, const QColor &color); // 字体/颜色作用于选中文字
    void setTemplateText(const QStringList &lines);       // 患者信息模板文字（替换旧的）
    void removeSelected();
    void clearContent();                                   // 清空图片与文字，保留分区
    void renderContent(QPainter *painter, const QRectF &target); // 白底、无分区的共用渲染
    bool deserializeScene(const QByteArray &data, int *missingOut = nullptr); // 恢复场景（撤销/打开工程用）
    bool exportImage(const QString &path, qreal scale);    // 按后缀导出 PNG/JPEG
    bool exportPdf(const QString &path);
    bool saveProject(const QString &path);                 // 工程文件
    bool loadProject(const QString &path);
    void pushUndoSnapshot();                               // 记录当前状态（撤销用）
    void undo();                                           // 撤销一步（Ctrl+Z）
    PhotoZone *zoneAt(const QPointF &scenePos) const;
    void requestMoveBetweenZones(PhotoItem *item, const QPointF &scenePos);
    static bool canAcceptImageDrop(const QMimeData *mime);
    static QStringList imagePathsFromMime(const QMimeData *mime);

signals:
    void statusMessage(const QString &message);
    void layoutChanged(int index);

protected:
    bool viewportEvent(QEvent *event) override;
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
    TextItem *createTextItem(const QString &text);
    void fitScene();
    QByteArray serializeScene() const;

    QGraphicsScene m_scene;
    QList<PhotoZone *> m_zones;
    QList<TextItem *> m_templateItems;
    QList<QByteArray> m_undoStack;
    int m_layoutIndex = 0;
    int m_textPixelSize = 28;
    QColor m_textColor = QColor(0x1f, 0x29, 0x37);
    bool m_undoSuspended = false;
};
