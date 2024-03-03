#include "aboutbox.h"
#include "ui_aboutbox.h"
#include "mainwnd.h"
#include "../brain.h"
#include "../netclient.h"

AboutBox::AboutBox(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutBox)
{
    ui->setupUi(this);
    ui->verBrain->setText(ANNA_VERSION);
    ui->verGUI->setText(AG_VERSION);
    ui->verNC->setText(ANNA_CLIENT_VERSION);
    ui->portrait_2->setPixmap(QPixmap::fromImage(ui->portrait_2->pixmap(Qt::ReturnByValue).toImage().mirrored(true,false)));
}

AboutBox::~AboutBox()
{
    delete ui;
}
