#include "helpbox.h"
#include "ui_helpbox.h"

HelpBox::HelpBox(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::HelpBox)
{
    ui->setupUi(this);
}

HelpBox::~HelpBox()
{
    delete ui;
}

void HelpBox::on_pushButton_4_clicked()
{
    close();
}

