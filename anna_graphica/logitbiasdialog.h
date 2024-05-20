#ifndef LOGITBIASDIALOG_H
#define LOGITBIASDIALOG_H

#include <QDialog>
#include <string>
#include <list>
#include "brain.h"

enum AnnaLogBiasOp {
    ALB_OP_NOP,
    ALB_OP_ADD,
    ALB_OP_MUL,
    ALB_OP_SET,
    ALB_OP_N_OPS
};

struct AnnaLogBias {
    QString pat;
    AnnaLogBiasOp op;
    double val;
};

namespace Ui {
class LogitBiasDialog;
}

class LogitBiasDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LogitBiasDialog(QWidget *parent);
    ~LogitBiasDialog();

    AnnaBrain* brain = nullptr;
    std::vector<AnnaLogBias> biases;

protected:
    virtual void showEvent(QShowEvent *event) override;

private slots:
    void on_patLine_textChanged(const QString &arg1);

    void on_btnAdd_clicked();

    void on_LogitBiasDialog_accepted();

    void on_btnRemove_clicked();

    void on_btnClear_clicked();

    void on_btnSave_clicked();

    void on_btnLoad_clicked();

private:
    Ui::LogitBiasDialog *ui;
    std::list<std::string> vocab;

    void UpdateTab();
};

#endif // LOGITBIASDIALOG_H
