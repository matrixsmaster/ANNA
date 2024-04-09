#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QSettings>
#include <QFontDialog>
#include "../brain.h"

#define AG_SETS_RQP_TAB 6

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
    static int tab_idx;

    static void LoadSettings(AnnaConfig* cfg, QSettings* sets);
    static void SaveSettings(AnnaConfig* cfg, QSettings* sets);

protected:
    virtual void showEvent(QShowEvent *event) override;

private slots:
    void on_buttonBox_accepted();

    void on_tempKnob_valueChanged(int value);

    void on_tempEdit_valueChanged(double arg1);

    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

    void on_gaFactor_editingFinished();

    void on_gaWidth_editingFinished();

    void on_gaFactor_valueChanged(int arg1);

    void on_pushButton_3_clicked();

    void on_pushButton_4_clicked();

    void on_pushButton_5_clicked();

    void on_pushButton_6_clicked();

    void on_pushButton_7_clicked();

    void on_pushButton_8_clicked();

private:
    Ui::SettingsDialog *ui;

    QString GetSaveFileName(QString title, QString filter, QString ext);
    static QFont LoadFont(QSettings* sets, QString prefix, const QFont& prev);
    void addRQPfile(QString fn, bool checked);
};

#endif // SETTINGSDIALOG_H
