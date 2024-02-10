#include "rqpeditor.h"
#include "ui_rqpeditor.h"

RQPEditor::RQPEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RQPEditor)
{
    ui->setupUi(this);
}

RQPEditor::~RQPEditor()
{
    delete ui;
}
