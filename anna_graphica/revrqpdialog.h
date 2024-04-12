#ifndef REVRQPDIALOG_H
#define REVRQPDIALOG_H

#include <QDialog>

namespace Ui {
class RevRQPDialog;
}

class RevRQPDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RevRQPDialog(QWidget *parent = nullptr);
    ~RevRQPDialog();

    void setCode(QString cmd, QStringList args);
    void getCode(QString& cmd, QStringList& args);

private:
    Ui::RevRQPDialog *ui;
};

#endif // REVRQPDIALOG_H
