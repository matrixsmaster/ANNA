#include <QMessageBox>
#include <QSettings>
#include <QFileDialog>
#include "logitbiasdialog.h"
#include "ui_logitbiasdialog.h"
#include "mainwnd.h"

using namespace std;

static const char* opnames[ALB_OP_N_OPS] = {
    "Nop", "Add", "Mul", "Set"
};

LogitBiasDialog::LogitBiasDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LogitBiasDialog)
{
    ui->setupUi(this);
    for (auto i : opnames) ui->opCombo->addItem(i);
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
        if (reg.exactMatch(str))
            ui->patList->addItem(str);
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
    b.op = (AnnaLogBiasOp)ui->opCombo->currentIndex();
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
        ui->mainTab->setItem(i,1,(new QTableWidgetItem(opnames[biases.at(i).op])));
        ui->mainTab->setItem(i,2,(new QTableWidgetItem(QString::asprintf("%.3f",biases.at(i).val))));
    }
}

void LogitBiasDialog::on_btnRemove_clicked()
{
    int n = ui->mainTab->currentRow();
    if (n >= 0 && n < (int)biases.size())
        biases.erase(biases.begin() + n);
    UpdateTab();
}

void LogitBiasDialog::on_btnClear_clicked()
{
    if (QMessageBox::question(this,"Logit Bias Editor","Do you want to delete all records?",QMessageBox::No | QMessageBox::Yes,QMessageBox::No) == QMessageBox::Yes)
        biases.clear();
    UpdateTab();
}

void LogitBiasDialog::on_btnSave_clicked()
{
    MainWnd* mwnd = dynamic_cast<MainWnd*>(parent());
    QString fn = mwnd? mwnd->GetSaveFileName(ANNA_FILE_LOGITBIAS) :
                       QFileDialog::getSaveFileName(this,"Save bias config file","","INI files (*.ini);;All files (*.*)");
    if (fn.isEmpty()) return;

    QSettings ini(fn,QSettings::IniFormat);

    int n = biases.size();
    ini.beginWriteArray("bias",n);
    for (int i = 0; i < n; i++) {
        ini.setArrayIndex(i);
        ini.setValue("pat",biases.at(i).pat);
        ini.setValue("op",biases.at(i).op);
        ini.setValue("val",biases.at(i).val);
    }
    ini.endArray();
}

void LogitBiasDialog::on_btnLoad_clicked()
{
    MainWnd* mwnd = dynamic_cast<MainWnd*>(parent());
    QString fn = mwnd? mwnd->GetOpenFileName(ANNA_FILE_LOGITBIAS) :
                       QFileDialog::getOpenFileName(this,"Load bias config file","","INI files (*.ini);;All files (*.*)");
    if (fn.isEmpty()) return;

    QSettings ini(fn,QSettings::IniFormat);

    biases.clear();
    int n = ini.beginReadArray("bias");
    for (int i = 0; i < n; i++) {
        ini.setArrayIndex(i);

        AnnaLogBias b;
        b.pat = ini.value("pat").toString();
        b.op = static_cast<AnnaLogBiasOp>(ini.value("op").toInt());
        b.val = ini.value("val").toDouble();

        biases.push_back(b);
    }
    ini.endArray();
    UpdateTab();
}
