#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "mainwnd.h"

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
        ui->eosToNL->setChecked(pconfig->convert_eos_to_nl);
        ui->NLtoTurn->setChecked(pconfig->nl_to_turnover);

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

        AnnaGuiSettings* gs = (AnnaGuiSettings*)pconfig->user;
        ui->enterButn->setCurrentIndex(gs->enter_key);
        ui->fixMD->setChecked(gs->md_fix);
    }

    QDialog::showEvent(event);
}

void SettingsDialog::SaveSettings(AnnaConfig* cfg, QSettings* sets)
{
    if (!cfg || !sets) return;

    sets->beginGroup("Global");
    sets->setValue("eos_to_nl",cfg->convert_eos_to_nl);
    sets->setValue("nl_to_turnover",cfg->nl_to_turnover);

    gpt_params* p = &(cfg->params);
    sets->setValue("seed",p->seed);
    sets->setValue("cpu",p->n_threads);
    sets->setValue("max_tokens",p->n_predict);
    sets->setValue("context",p->n_ctx);
    sets->setValue("gpu",p->n_gpu_layers);

    llama_sampling_params* s = &(p->sampling_params);
    sets->endGroup();
    sets->beginGroup("Sampling");
    sets->setValue("topp",s->top_p);
    sets->setValue("topk",s->top_k);
    sets->setValue("tfs",s->tfs_z);
    sets->setValue("typp",s->typical_p);
    sets->setValue("mtau",s->mirostat_tau);
    sets->setValue("meta",s->mirostat_eta);
    sets->setValue("temp",s->temp);
    sets->setValue("rep_last_n",s->repeat_last_n);
    sets->setValue("rep_penalty",s->repeat_penalty);
    sets->setValue("mirostat",s->mirostat);

    AnnaGuiSettings* gs = (AnnaGuiSettings*)cfg->user;
    sets->endGroup();
    sets->beginGroup("UI");
    sets->setValue("enter_key",gs->enter_key);
    sets->setValue("md_fix",gs->md_fix);
}

void SettingsDialog::on_buttonBox_accepted()
{
    pconfig->convert_eos_to_nl = ui->eosToNL->isChecked();
    pconfig->nl_to_turnover = ui->NLtoTurn->isChecked();

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

    AnnaGuiSettings* gs = (AnnaGuiSettings*)pconfig->user;
    gs->enter_key = ui->enterButn->currentIndex();
    gs->md_fix = ui->fixMD->isChecked();
}

void SettingsDialog::LoadSettings(AnnaConfig* cfg, QSettings* sets)
{
    if (!cfg || !sets) return;

    sets->beginGroup("Global");
    cfg->convert_eos_to_nl = sets->value("eos_to_nl",cfg->convert_eos_to_nl).toBool();
    cfg->nl_to_turnover = sets->value("nl_to_turnover",cfg->nl_to_turnover).toBool();

    gpt_params* p = &(cfg->params);
    p->seed = sets->value("seed",p->seed).toUInt();
    p->n_threads = sets->value("cpu",p->n_threads).toInt();
    p->n_predict = sets->value("max_tokens",p->n_predict).toInt();
    p->n_ctx = sets->value("context",p->n_ctx).toInt();
    p->n_gpu_layers = sets->value("gpu",p->n_gpu_layers).toInt();

    llama_sampling_params* s = &(p->sampling_params);
    sets->endGroup();
    sets->beginGroup("Sampling");
    s->top_p = sets->value("topp",s->top_p).toFloat();
    s->top_k = sets->value("topk",s->top_k).toInt();
    s->tfs_z = sets->value("tfs",s->tfs_z).toFloat();
    s->typical_p = sets->value("typp",s->typical_p).toFloat();
    s->mirostat_tau = sets->value("mtau",s->mirostat_tau).toFloat();
    s->mirostat_eta = sets->value("meta",s->mirostat_eta).toFloat();
    s->temp = sets->value("temp",s->temp).toFloat();
    s->repeat_last_n = sets->value("rep_last_n",s->repeat_last_n).toInt();
    s->repeat_penalty = sets->value("rep_penalty",s->repeat_penalty).toFloat();
    s->mirostat = sets->value("mirostat",s->mirostat).toInt();

    AnnaGuiSettings* gs = (AnnaGuiSettings*)cfg->user;
    sets->endGroup();
    sets->beginGroup("UI");
    gs->enter_key = sets->value("enter_key",gs->enter_key).toInt();
    gs->md_fix = sets->value("md_fix",gs->md_fix).toBool();
}

void SettingsDialog::on_tempKnob_valueChanged(int value)
{
    ui->tempEdit->setValue(value / 100.f);
}

void SettingsDialog::on_tempEdit_valueChanged(double arg1)
{
    ui->tempKnob->setValue(floor(arg1 * 100.f));
}
