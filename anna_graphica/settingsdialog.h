#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include "brain.h"

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    AnnaConfig* pconfig;

protected:
    virtual void showEvent(QShowEvent *event) override;

private slots:
    void on_buttonBox_accepted();

    void on_tempKnob_valueChanged(int value);

    void on_tempEdit_valueChanged(double arg1);

private:
    Ui::SettingsDialog *ui;
};

#endif // SETTINGSDIALOG_H
