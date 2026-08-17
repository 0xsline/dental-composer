#pragma once

#include <QColor>
#include <QMainWindow>

class CanvasView;
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
    void onPickColor();
    void onClear();

private:
    void updateColorButton();

    CanvasView *m_canvas = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QToolButton *m_colorButton = nullptr;
    QColor m_textColor;
};
