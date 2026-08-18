#include "mainwindow.h"
#include "canvasview.h"
#include "exportdialog.h"
#include "patientinfodialog.h"

#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
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

    // ---------- 菜单 ----------
    QMenu *fileMenu = menuBar()->addMenu(tr("文件"));
    QAction *actOpenProject = fileMenu->addAction(tr("打开工程…"));
    QAction *actSaveProject = fileMenu->addAction(tr("保存工程…"));
    fileMenu->addSeparator();
    QAction *actExportPdf = fileMenu->addAction(tr("导出 PDF…"));
    QAction *actPrintPreview = fileMenu->addAction(tr("打印预览…"));
    QAction *actPrint = fileMenu->addAction(tr("打印…"));
    fileMenu->addSeparator();
#ifndef Q_OS_MAC
    fileMenu->addAction(tr("退出"), qApp, &QCoreApplication::quit);
#endif

    QMenu *editMenu = menuBar()->addMenu(tr("编辑"));
    QAction *actPatient = editMenu->addAction(tr("患者信息…"));
    editMenu->addSeparator();

    QMenu *helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(tr("关于…"), this, &MainWindow::onAbout);

    // ---------- 工具栏 ----------
    QToolBar *toolBar = addToolBar(tr("工具栏"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    QAction *actImport = toolBar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("导入图片"));
    actImport->setToolTip(tr("选择图片文件，依次装入空分区"));
    toolBar->addAction(actPatient);
    actPatient->setToolTip(tr("填写患者信息，自动生成临床文字"));
    QAction *actUndo = toolBar->addAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("撤销"));
    actUndo->setShortcut(QKeySequence::Undo);
    actUndo->setToolTip(tr("撤销上一步操作（Ctrl+Z）"));
    editMenu->addAction(actUndo);
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
    m_fontSize->setToolTip(tr("文字字号（选中文字时直接生效）"));
    toolBar->addWidget(m_fontSize);

    m_colorButton = new QToolButton(toolBar);
    m_colorButton->setText(tr("颜色"));
    m_colorButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_colorButton->setAutoRaise(false);
    m_colorButton->setCursor(Qt::PointingHandCursor);
    m_colorButton->setToolTip(tr("文字颜色（选中文字时直接生效）"));
    m_colorButton->setStyleSheet(
        QStringLiteral("QToolButton { padding: 2px 8px 2px 4px; border: 1px solid #8a8a8a; "
                       "border-radius: 4px; background: #f3f3f3; color: #1a1a1a; }"
                       "QToolButton:hover { border-color: #3b82f6; background: #ffffff; }"));
    toolBar->addWidget(m_colorButton);

    toolBar->addSeparator();
    toolBar->addWidget(new QLabel(tr(" 布局 "), toolBar));
    m_layoutBox = new QComboBox(toolBar);
    for (int i = 0; i < m_canvas->layoutCount(); ++i)
        m_layoutBox->addItem(m_canvas->layoutName(i));
    m_layoutBox->setCurrentIndex(m_canvas->currentLayout());
    m_layoutBox->setToolTip(tr("画布布局模板（切换会清空画布）"));
    toolBar->addWidget(m_layoutBox);

    // ---------- 信号 ----------
    connect(actImport, &QAction::triggered, this, &MainWindow::onImport);
    connect(actText, &QAction::triggered, this, &MainWindow::onAddText);
    connect(actPatient, &QAction::triggered, this, &MainWindow::onPatientInfo);
    connect(actUndo, &QAction::triggered, m_canvas, &CanvasView::undo);
    connect(actRemove, &QAction::triggered, m_canvas, &CanvasView::removeSelected);
    connect(actClear, &QAction::triggered, this, &MainWindow::onClear);
    connect(actExport, &QAction::triggered, this, &MainWindow::onExport);
    connect(actOpenProject, &QAction::triggered, this, &MainWindow::onOpenProject);
    connect(actSaveProject, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(actExportPdf, &QAction::triggered, this, &MainWindow::onExportPdf);
    connect(actPrint, &QAction::triggered, this, &MainWindow::onPrint);
    connect(actPrintPreview, &QAction::triggered, this, &MainWindow::onPrintPreview);
    connect(m_layoutBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLayoutChanged);
    connect(m_canvas, &CanvasView::layoutChanged, this, [this](int index) {
        m_layoutBox->blockSignals(true);
        m_layoutBox->setCurrentIndex(index);
        m_layoutBox->blockSignals(false);
    });
    connect(m_fontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_canvas->setTextStyle(value, m_textColor);
        m_canvas->applyStyleToSelection(value, m_textColor);
    });
    connect(m_colorButton, &QToolButton::clicked, this, &MainWindow::onPickColor);

    m_textColor = QColor(0x1f, 0x29, 0x37);
    m_canvas->setTextStyle(m_fontSize->value(), m_textColor);
    updateColorButton();

    const QString hint = tr("拖入图片到任意分区 · 滚轮等比缩放 · 拖图片边角改大小 · 双击图片适应分区");
    statusBar()->showMessage(hint);
    connect(m_canvas, &CanvasView::statusMessage, this, [this, hint](const QString &message) {
        statusBar()->showMessage(message, 5000);
        // 瞬态消息过期后恢复常驻操作提示
        QTimer::singleShot(5200, this, [this, hint]() { statusBar()->showMessage(hint); });
    });
}

void MainWindow::updateColorButton()
{
    const int s = 22;
    QPixmap pixmap(s, s);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box(1.5, 1.5, s - 3.0, s - 3.0);
    painter.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
    painter.setBrush(m_textColor);
    painter.drawRoundedRect(box, 4.0, 4.0);
    painter.setPen(QPen(QColor(255, 255, 255, 230), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box.adjusted(1.0, 1.0, -1.0, -1.0), 3.0, 3.0);
    painter.end();
    m_colorButton->setIcon(QIcon(pixmap));
    m_colorButton->setIconSize(QSize(s, s));
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

void MainWindow::onPatientInfo()
{
    PatientInfoDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_canvas->setTemplateText(dialog.lines());
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
    m_canvas->applyStyleToSelection(m_fontSize->value(), color);
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

void MainWindow::onExportPdf()
{
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("导出 PDF"),
        defaultDir + QStringLiteral("/") + tr("拼图结果.pdf"), tr("PDF 文件 (*.pdf)"));
    if (path.isEmpty())
        return;
    if (m_canvas->exportPdf(path))
        statusBar()->showMessage(tr("已导出 PDF：%1").arg(QFileInfo(path).fileName()), 8000);
    else
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件：\n%1").arg(path));
}

void MainWindow::printContent(QPrinter *printer)
{
    QPainter painter;
    if (!painter.begin(printer)) {
        QMessageBox::warning(this, tr("打印"), tr("无法初始化打印机"));
        return;
    }
    const qreal margin = 15.0 / 25.4 * printer->resolution();
    m_canvas->renderContent(&painter,
        QRectF(margin, margin, printer->width() - 2 * margin, printer->height() - 2 * margin));
    painter.end();
}

void MainWindow::onPrint()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Landscape);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    printContent(&printer);
}

void MainWindow::onPrintPreview()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Landscape);
    QPrintPreviewDialog preview(&printer, this);
    connect(&preview, &QPrintPreviewDialog::paintRequested, this, [this](QPrinter *p) {
        printContent(p);
    });
    preview.exec();
}

void MainWindow::onSaveProject()
{
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("保存工程"),
        defaultDir + QStringLiteral("/") + tr("未命名.dcp"), tr("牙片拼图工程 (*.dcp)"));
    if (path.isEmpty())
        return;
    if (m_canvas->saveProject(path))
        statusBar()->showMessage(tr("已保存工程：%1").arg(QFileInfo(path).fileName()), 8000);
    else
        QMessageBox::warning(this, tr("保存失败"), tr("无法写入文件：\n%1").arg(path));
}

void MainWindow::onOpenProject()
{
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(this, tr("打开工程"),
        defaultDir, tr("牙片拼图工程 (*.dcp)"));
    if (path.isEmpty())
        return;
    if (!m_canvas->loadProject(path))
        QMessageBox::warning(this, tr("打开失败"), tr("无法读取工程文件：\n%1").arg(path));
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("关于牙片拼图"),
        tr("<b>牙片拼图</b> v0.1.3<br><br>"
           "牙科影像四格/三格拼图工具：图片拖放、患者信息、文字叠加、高清导出与打印。"
           "<br>支持 Win7 SP1 64 位及以上、macOS。<br><br>"
           "作者：晨旭口腔 · 袁萍医生团队"));
}
