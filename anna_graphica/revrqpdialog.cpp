#include "revrqpdialog.h"
#include "ui_revrqpdialog.h"

RevRQPDialog::RevRQPDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RevRQPDialog)
{
    ui->setupUi(this);
}

RevRQPDialog::~RevRQPDialog()
{
    delete ui;
}

void RevRQPDialog::setCode(QString cmd, QStringList args)
{
    ui->cmdEdit->setText(cmd);

    ui->argsTab->clear();
    ui->argsTab->setColumnCount(1);
    ui->argsTab->setRowCount(args.size());
    for (int i = 0; i < ui->argsTab->rowCount(); i++) {
        QTableWidgetItem* itm = new QTableWidgetItem(args.at(i));
        ui->argsTab->setItem(i,0,itm);
    }
}

void RevRQPDialog::getCode(QString& cmd, QStringList& args)
{
    cmd = ui->cmdEdit->text();

    args.clear();
    for (int i = 0; i < ui->argsTab->rowCount(); i++)
        args.push_back(ui->argsTab->item(i,0)->text());
}
