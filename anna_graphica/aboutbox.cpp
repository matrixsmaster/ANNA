#include "aboutbox.h"
#include "ui_aboutbox.h"
#include "brain.h"

AboutBox::AboutBox(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutBox)
{
    ui->setupUi(this);
    ui->version->setText("ver. " ANNA_VERSION);
    ui->copyright->setText(COPYRIGHT);
    ui->portrait_2->setPixmap(QPixmap::fromImage(ui->portrait_2->pixmap()->toImage().mirrored(true,false)));
}

AboutBox::~AboutBox()
{
    delete ui;
}
