#pragma once

#include <QDialog>

class QButtonGroup;
class QDoubleSpinBox;

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExportDialog(QWidget *parent = nullptr);

    qreal scale() const;

private:
    QButtonGroup *m_group = nullptr;
    QDoubleSpinBox *m_custom = nullptr;
};
