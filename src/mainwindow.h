#pragma once

#include <QColor>
#include <QMainWindow>

class CanvasView;
class QComboBox;
class QPrinter;
class QSpinBox;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    CanvasView *canvas() const { return m_canvas; }

private slots:
    void onImport();
    void onAddText();
    void onExport();
    void onExportPdf();
    void onPrint();
    void onPrintPreview();
    void onSaveProject();
    void onOpenProject();
    void onPatientInfo();
    void onAbout();
    void onPickColor();
    void onClear();
    void onLayoutChanged(int index);
    void onRotateLeft();
    void onRotateRight();
    void onCopyImage();
    void onPasteImage();

private:
    void updateColorButton();
    void printContent(QPrinter *printer);

    CanvasView *m_canvas = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QToolButton *m_colorButton = nullptr;
    QComboBox *m_layoutBox = nullptr;
    QColor m_textColor;
};
