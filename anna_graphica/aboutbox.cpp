#include "aboutbox.h"
#include "ui_aboutbox.h"
#include "brain.h"
#include "mainwnd.h"

AboutBox::AboutBox(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutBox)
{
    ui->setupUi(this);
    ui->verBrain->setText(ANNA_VERSION);
    ui->verGUI->setText(AG_VERSION);
    ui->portrait_2->setPixmap(QPixmap::fromImage(ui->portrait_2->pixmap()->toImage().mirrored(true,false)));
}

AboutBox::~AboutBox()
{
    delete ui;
}
