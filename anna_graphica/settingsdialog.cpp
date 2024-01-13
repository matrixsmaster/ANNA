#include "settingsdialog.h"
#include "ui_settingsdialog.h"

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    pconfig(nullptr),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    ui->Seed->setMinimum(std::numeric_limits<int>::min());
    ui->Seed->setMaximum(std::numeric_limits<int>::max());
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    if (pconfig) {
        pconfig->convert_eos_to_nl = ui->eosToNL->isChecked();

        gpt_params* p = &(pconfig->params);
        ui->Seed->setValue(p->seed);
        ui->n_cpu->setValue(p->n_threads);
        ui->maxTok->setValue(p->n_predict);
        ui->conLen->setValue(p->n_ctx);
        ui->n_gpu->setValue(p->n_gpu_layers);

        llama_sampling_params* s = &(p->sampling_params);
        ui->topP->setValue(s->top_p);
        ui->topK->setValue(s->top_k);
        ui->tailFree->setValue(s->tfs_z);
        ui->typP->setValue(s->typical_p);
        ui->Entropy->setValue(s->mirostat_tau);
        ui->learnRate->setValue(s->mirostat_eta);
        ui->tempEdit->setValue(s->temp);
        ui->lastN->setValue(s->repeat_last_n);
        ui->repPenalty->setValue(s->repeat_penalty);

        if (s->mirostat == 1)
            ui->sampMiro1->setChecked(true);
        else if (s->mirostat == 2)
            ui->sampMiro2->setChecked(true);
        else if (s->temp < 0)
            ui->sampGreedy->setChecked(true);
        else
            ui->sampTopP->setChecked(true);
    }

    QDialog::showEvent(event);
}

void SettingsDialog::on_buttonBox_accepted()
{
    pconfig->convert_eos_to_nl = ui->eosToNL->isChecked();

    gpt_params* p = &(pconfig->params);
    p->seed = ui->Seed->value();
    p->n_threads = ui->n_cpu->value();
    p->n_predict = ui->maxTok->value();
    p->n_ctx = ui->conLen->value();
    p->n_gpu_layers = ui->n_gpu->value();

    llama_sampling_params* s = &(p->sampling_params);
    s->top_p = ui->topP->value();
    s->top_k = ui->topK->value();
    s->tfs_z = ui->tailFree->value();
    s->typical_p = ui->typP->value();
    s->mirostat_tau = ui->Entropy->value();
    s->mirostat_eta = ui->learnRate->value();
    s->temp = ui->tempEdit->value();
    s->repeat_last_n = ui->lastN->value();
    s->repeat_penalty = ui->repPenalty->value();

    s->mirostat = 0;
    if (ui->sampMiro1->isChecked() || ui->sampMiro2->isChecked())
        s->mirostat = ui->sampMiro1->isChecked()? 1:2;
    else if (ui->sampGreedy->isChecked())
        s->temp = -1;
}

void SettingsDialog::on_tempKnob_valueChanged(int value)
{
    ui->tempEdit->setValue(value / 100.f);
}

void SettingsDialog::on_tempEdit_valueChanged(double arg1)
{
    ui->tempKnob->setValue(floor(arg1 * 100.f));
}
