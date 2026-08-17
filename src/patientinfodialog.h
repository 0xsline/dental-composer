#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLineEdit;

// 患者信息填写：生成临床文字行（仿手写报告样式）
class PatientInfoDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PatientInfoDialog(QWidget *parent = nullptr);

    QStringList lines() const; // 生成文字行，如 ["8.18号，某某，袁萍医生，初诊", "7Y 8M，主诉：窝沟复查"]

private:
    QLineEdit *m_date = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_phone = nullptr;
    QLineEdit *m_age = nullptr;
    QComboBox *m_sex = nullptr;
    QLineEdit *m_doctor = nullptr;
    QComboBox *m_visitType = nullptr;
    QLineEdit *m_complaint = nullptr;
    QLineEdit *m_note = nullptr;
};
