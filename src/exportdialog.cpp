#include "exportdialog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

ExportDialog::ExportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("导出分辨率"));
    auto *layout = new QVBoxLayout(this);

    auto *r1 = new QRadioButton(tr("屏幕（1600 × 1100）"), this);
    auto *r2 = new QRadioButton(tr("高清（3200 × 2200）"), this);
    auto *r3 = new QRadioButton(tr("打印（4800 × 3300）"), this);
    r2->setChecked(true);

    m_group = new QButtonGroup(this);
    m_group->addButton(r1, 0);
    m_group->addButton(r2, 1);
    m_group->addButton(r3, 2);
    layout->addWidget(r1);
    layout->addWidget(r2);
    layout->addWidget(r3);

    auto *customRow = new QHBoxLayout;
    auto *rCustom = new QRadioButton(tr("自定义："), this);
    m_group->addButton(rCustom, 3);
    m_custom = new QDoubleSpinBox(this);
    m_custom->setRange(0.5, 8.0);
    m_custom->setSingleStep(0.5);
    m_custom->setDecimals(1);
    m_custom->setValue(2.0);
    m_custom->setSuffix(tr(" 倍"));
    customRow->addWidget(rCustom);
    customRow->addWidget(m_custom);
    customRow->addStretch(1);
    layout->addLayout(customRow);

    layout->addWidget(new QLabel(tr("提示：导出仅包含图片与文字，分区边框不会出现。"), this));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

qreal ExportDialog::scale() const
{
    switch (m_group->checkedId()) {
    case 0:
        return 1.0;
    case 1:
        return 2.0;
    case 2:
        return 3.0;
    case 3:
        return m_custom->value();
    default:
        return 2.0;
    }
}
