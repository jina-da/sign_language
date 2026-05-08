#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QRadioButton>
#include <QCheckBox>

QT_BEGIN_NAMESPACE
namespace Ui { class RegisterWidget; }
QT_END_NAMESPACE

class RegisterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RegisterWidget(QWidget *parent = nullptr);
    ~RegisterWidget();

    void showError(const QString &message);
    void setLoading(bool loading);

signals:
    void registerRequested(const QString &username,
                           const QString &passwordHash,
                           bool isDeaf,
                           bool isDominantLeft,
                           bool keypointConsent);
    void backToLogin();

private slots:
    void onRegisterClicked();

private:
    QString hashPassword(const QString &plain) const;

    Ui::RegisterWidget *ui;
};
