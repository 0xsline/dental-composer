#include "patientinfodialog.h"

#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSettings>
#include <QPushButton>
#include <QVBoxLayout>

PatientInfoDialog::PatientInfoDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("患者信息"));
    auto *form = new QFormLayout;

    m_date = new QLineEdit(QDate::currentDate().toString(QStringLiteral("M.d号")), this);
    m_name = new QLineEdit(this);
    m_phone = new QLineEdit(this);
    m_age = new QLineEdit(this);
    m_age->setPlaceholderText(tr("如：7Y 8M 或 71"));
    m_sex = new QComboBox(this);
    m_sex->addItems({ QString(), tr("男"), tr("女") });
    QSettings settings;
    m_doctor = new QLineEdit(settings.value(QStringLiteral("patient/doctor"),
                                            QStringLiteral("袁萍")).toString(), this);
    m_visitType = new QComboBox(this);
    m_visitType->addItems({ tr("初诊"), tr("复诊") });
    m_complaint = new QLineEdit(this);
    m_note = new QLineEdit(this);

    form->addRow(tr("日期"), m_date);
    form->addRow(tr("姓名"), m_name);
    form->addRow(tr("电话"), m_phone);
    form->addRow(tr("年龄"), m_age);
    form->addRow(tr("性别"), m_sex);
    form->addRow(tr("医生"), m_doctor);
    form->addRow(tr("类型"), m_visitType);
    form->addRow(tr("主诉"), m_complaint);
    form->addRow(tr("备注"), m_note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("生成文字"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        // 记住医生默认值
        QSettings settings;
        settings.setValue(QStringLiteral("patient/doctor"), m_doctor->text());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QStringList PatientInfoDialog::lines() const
{
    QStringList out;

    QString line1 = m_date->text();
    if (!m_name->text().isEmpty())
        line1 += QStringLiteral("，") + m_name->text();
    line1 += QStringLiteral("，") + m_doctor->text() + tr("医生");
    line1 += QStringLiteral("，") + m_visitType->currentText();
    out << line1;

    QString line2;
    if (!m_phone->text().isEmpty())
        line2 += m_phone->text() + QStringLiteral("  ");
    if (!m_age->text().isEmpty())
        line2 += m_age->text() + QStringLiteral("，");
    if (!m_sex->currentText().isEmpty())
        line2 += m_sex->currentText() + QStringLiteral("，");
    line2 += QStringLiteral("主诉：") + m_complaint->text();
    out << line2;

    if (!m_note->text().isEmpty())
        out << QStringLiteral("备注：") + m_note->text();

    return out;
}
