#include "logitbiasdialog.h"
#include "ui_logitbiasdialog.h"

using namespace std;

LogitBiasDialog::LogitBiasDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LogitBiasDialog)
{
    ui->setupUi(this);
}

LogitBiasDialog::~LogitBiasDialog()
{
    delete ui;
}

void LogitBiasDialog::showEvent(QShowEvent *event)
{
    if (brain) vocab = brain->getDictionary();
    else vocab.clear();

    UpdateTab();

    QDialog::showEvent(event);
}

void LogitBiasDialog::on_patLine_textChanged(const QString &arg1)
{
    if (vocab.empty()) return;

    QRegExp reg(arg1);
    if (!reg.isValid()) return;

    ui->patList->clear();
    for (auto &&i : vocab) {
        QString str = QString::fromStdString(i);
        if (reg.exactMatch(str)) ui->patList->addItem(str);
    }
}

void LogitBiasDialog::on_btnAdd_clicked()
{
    QString str = ui->patLine->text();
    if (str.isEmpty()) return;
    QRegExp reg(str);
    if (!reg.isValid()) return;

    AnnaLogBias b;
    b.pat = str;
    b.op = ui->opAdd->isChecked()? ALB_OP_ADD : ALB_OP_MUL;
    b.val = ui->magnitude->value();
    biases.push_back(b);

    UpdateTab();
}

void LogitBiasDialog::on_LogitBiasDialog_accepted()
{
    if (!brain) return;

    for (auto &&i : biases) {
        QRegExp reg(i.pat);
        if (!reg.isValid()) continue;

        llama_sample_bias b;
        b.tok = -1;
        b.op = i.op;
        b.val = i.val;

        for (auto &&j : vocab) {
            b.tok++;
            if (reg.exactMatch(QString::fromStdString(j)))
                brain->applyLogitBias(b);
        }
    }
}

void LogitBiasDialog::UpdateTab()
{
    ui->mainTab->clear();
    ui->mainTab->setColumnCount(3);
    QStringList header = {"Pattern", "Operation", "Magnitude"};
    ui->mainTab->setHorizontalHeaderLabels(header);
    ui->mainTab->setRowCount(biases.size());
    for (int i = 0; i < ui->mainTab->rowCount(); i++) {
        ui->mainTab->setItem(i,0,(new QTableWidgetItem(biases.at(i).pat)));
        ui->mainTab->setItem(i,1,(new QTableWidgetItem((biases.at(i).op == ALB_OP_ADD)? "Add":"Mul")));
        ui->mainTab->setItem(i,2,(new QTableWidgetItem(QString::asprintf("%.3f",biases.at(i).val))));
    }
}
