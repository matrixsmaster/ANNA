#include "rqpeditor.h"
#include "ui_rqpeditor.h"

RQPEditor::RQPEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RQPEditor),
    sets(nullptr)
{
    ui->setupUi(this);
}

RQPEditor::~RQPEditor()
{
    if (sets) delete sets;
    delete ui;
}

void RQPEditor::showEvent(QShowEvent* event)
{
    if (sets) delete sets;
    sets = new QSettings(filename);

    sets->beginGroup("MAIN");
    ui->startTag->setText(sets->value("start_tag",QString()).toString());
    ui->stopTag->setText(sets->value("stop_tag",QString()).toString());
    ui->command->setText(sets->value("command",QString()).toString());
    ui->filter->setText(sets->value("filter",QString()).toString());
    ui->args->setText(sets->value("args",QString()).toString());

    QDialog::showEvent(event);
}

void RQPEditor::on_buttonBox_accepted()
{
    if (!sets) return;
    sets->beginGroup("MAIN");
    sets->setValue("start_tag",ui->startTag->text());
    sets->setValue("stop_tag",ui->stopTag->text());
    sets->setValue("command",ui->command->text());
    sets->setValue("filter",ui->filter->text());
    sets->setValue("args",ui->args->text());
}

QStringList RQPEditor::DetectRQP(const QString &in, AnnaRQPState *st)
{
    //
}

void RQPEditor::on_testEdit_textChanged()
{
    rescan();
}

void RQPEditor::rescan()
{
    //
}
