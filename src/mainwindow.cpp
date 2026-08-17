#include "mainwindow.h"
#include "canvasview.h"
#include "exportdialog.h"

#include <QAction>
#include <QTimer>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("牙片拼图 — Dental Composer"));
    resize(1240, 900);

    m_canvas = new CanvasView(this);
    setCentralWidget(m_canvas);
#if defined(Q_OS_MAC)
    setUnifiedTitleAndToolBarOnMac(true);
#endif

    QToolBar *toolBar = addToolBar(tr("工具栏"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    QAction *actImport = toolBar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("导入图片"));
    actImport->setToolTip(tr("选择图片文件，依次装入空分区"));
    QAction *actText = toolBar->addAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), tr("添加文字"));
    actText->setToolTip(tr("在画布上添加一条文字，双击编辑"));
    QAction *actRemove = toolBar->addAction(style()->standardIcon(QStyle::SP_TrashIcon), tr("移除选中"));
    QAction *actClear = toolBar->addAction(tr("清空画布"));
    QAction *actExport = toolBar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("导出图片"));

    toolBar->addSeparator();
    toolBar->addWidget(new QLabel(tr(" 字号 "), toolBar));
    m_fontSize = new QSpinBox(toolBar);
    m_fontSize->setRange(10, 120);
    m_fontSize->setValue(28);
    m_fontSize->setSuffix(tr(" px"));
    m_fontSize->setToolTip(tr("新文字的字号"));
    toolBar->addWidget(m_fontSize);

    m_colorButton = new QToolButton(toolBar);
    m_colorButton->setToolTip(tr("新文字的颜色"));
    toolBar->addWidget(m_colorButton);

    toolBar->addSeparator();
    toolBar->addWidget(new QLabel(tr(" 布局 "), toolBar));
    m_layoutBox = new QComboBox(toolBar);
    for (int i = 0; i < m_canvas->layoutCount(); ++i)
        m_layoutBox->addItem(m_canvas->layoutName(i));
    m_layoutBox->setCurrentIndex(m_canvas->currentLayout());
    m_layoutBox->setToolTip(tr("画布布局模板（切换会清空画布）"));
    toolBar->addWidget(m_layoutBox);
    connect(m_layoutBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLayoutChanged);

    connect(actImport, &QAction::triggered, this, &MainWindow::onImport);
    connect(actText, &QAction::triggered, this, &MainWindow::onAddText);
    connect(actRemove, &QAction::triggered, m_canvas, &CanvasView::removeSelected);
    connect(actClear, &QAction::triggered, this, &MainWindow::onClear);
    connect(actExport, &QAction::triggered, this, &MainWindow::onExport);
    connect(m_fontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_canvas->setTextStyle(value, m_textColor);
    });
    connect(m_colorButton, &QToolButton::clicked, this, &MainWindow::onPickColor);
    m_textColor = QColor(0x1f, 0x29, 0x37);
    m_canvas->setTextStyle(m_fontSize->value(), m_textColor);
    updateColorButton();

    const QString hint = tr("拖入图片到任意分区 · 双击文字编辑 · 滚轮缩放 · 双击图片适应分区");
    statusBar()->showMessage(hint);
    connect(m_canvas, &CanvasView::statusMessage, this, [this, hint](const QString &message) {
        statusBar()->showMessage(message, 5000);
        // 瞬态消息过期后恢复常驻操作提示
        QTimer::singleShot(5200, this, [this, hint]() { statusBar()->showMessage(hint); });
    });
}

void MainWindow::updateColorButton()
{
    QPixmap pixmap(20, 20);
    pixmap.fill(m_textColor);
    m_colorButton->setIcon(QIcon(pixmap));
    m_colorButton->setIconSize(QSize(20, 20));
}

void MainWindow::onImport()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("选择图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff)"));
    if (!files.isEmpty())
        m_canvas->importImages(files);
}

void MainWindow::onAddText()
{
    m_canvas->addText(tr("双击编辑文字"));
}

void MainWindow::onClear()
{
    const auto answer = QMessageBox::question(this, tr("清空画布"),
        tr("确定清空画布上的全部图片与文字？"));
    if (answer == QMessageBox::Yes)
        m_canvas->clearContent();
}

void MainWindow::onLayoutChanged(int index)
{
    if (m_canvas->currentLayout() == index)
        return;
    const auto answer = QMessageBox::question(this, tr("切换布局"),
        tr("切换布局会清空画布上的图片与文字，确定切换？"));
    if (answer != QMessageBox::Yes) {
        m_layoutBox->blockSignals(true);
        m_layoutBox->setCurrentIndex(m_canvas->currentLayout());
        m_layoutBox->blockSignals(false);
        return;
    }
    m_canvas->applyLayout(index);
    statusBar()->showMessage(tr("已切换到“%1”布局").arg(m_canvas->layoutName(index)), 5000);
}

void MainWindow::onPickColor()
{
    const QColor color = QColorDialog::getColor(m_textColor, this, tr("文字颜色"));
    if (!color.isValid())
        return;
    m_textColor = color;
    m_canvas->setTextStyle(m_fontSize->value(), color);
    updateColorButton();
}

void MainWindow::onExport()
{
    QString selectedFilter;
    // 默认存到用户"图片"目录：Win7 安装版运行时 cwd 是 Program Files（无写权限），
    // 相对默认名会导致"导出失败"
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("导出图片"),
        defaultDir + QStringLiteral("/") + tr("拼图结果.png"),
        tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"), &selectedFilter);
    if (path.isEmpty())
        return;
    ExportDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (m_canvas->exportImage(path, dialog.scale())) {
        statusBar()->showMessage(tr("已导出：%1").arg(QFileInfo(path).fileName()), 8000);
    } else {
        QMessageBox::warning(this, tr("导出失败"),
            tr("无法写入文件：\n%1\n\n请换一个有写入权限的位置（如桌面、图片文件夹）后重试。")
                .arg(path));
    }
}
