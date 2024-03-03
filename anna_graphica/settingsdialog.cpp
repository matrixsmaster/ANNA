#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "mainwnd.h"
#include "rqpeditor.h"

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
        ui->gaFactor->setValue(p->grp_attn_n);
        ui->gaWidth->setValue(p->grp_attn_w);

        if (p->grp_attn_n > 1)
            ui->ctxUseGA->setChecked(true);
        else
            ui->ctxRopeScale->setChecked(true);

        llama_sampling_params* s = &(p->sparams);
        ui->topP->setValue(s->top_p);
        ui->topK->setValue(s->top_k);
        ui->tailFree->setValue(s->tfs_z);
        ui->typP->setValue(s->typical_p);
        ui->Entropy->setValue(s->mirostat_tau);
        ui->learnRate->setValue(s->mirostat_eta);
        ui->tempEdit->setValue(s->temp);
        ui->lastN->setValue(s->penalty_last_n);
        ui->repPenalty->setValue(s->penalty_repeat);

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
        ui->savePrompt->setChecked(gs->save_prompt);
        ui->clearLog->setChecked(gs->clear_log);
        ui->serverAddr->setText(gs->server);
        ui->useServer->setChecked(gs->use_server);
        ui->useBusyBox->setChecked(gs->use_busybox);
        ui->chatLogExample->setFont(gs->log_fnt);
        ui->userInExample->setFont(gs->usr_fnt);
        ui->aaUsePrefix->setChecked(gs->use_attprefix);
        ui->mkDummy->setChecked(gs->mk_dummy);
        ui->aaPrefix->setText(gs->att_prefix);
        ui->taPrefix->setText(gs->txt_prefix);
        ui->taSuffix->setText(gs->txt_suffix);

        ui->rqpList->clear();
        for (auto & i : gs->rqps) addRQPfile(i.fn,i.enabled);
    }

    ui->gaWidth->setSingleStep(1);
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
    sets->setValue("ga_factor",p->grp_attn_n);
    sets->setValue("ga_width",p->grp_attn_w);

    llama_sampling_params* s = &(p->sparams);
    sets->endGroup();
    sets->beginGroup("Sampling");
    sets->setValue("topp",s->top_p);
    sets->setValue("topk",s->top_k);
    sets->setValue("tfs",s->tfs_z);
    sets->setValue("typp",s->typical_p);
    sets->setValue("mtau",s->mirostat_tau);
    sets->setValue("meta",s->mirostat_eta);
    sets->setValue("temp",s->temp);
    sets->setValue("rep_last_n",s->penalty_last_n);
    sets->setValue("rep_penalty",s->penalty_repeat);
    sets->setValue("mirostat",s->mirostat);

    AnnaGuiSettings* gs = (AnnaGuiSettings*)cfg->user;
    sets->endGroup();
    sets->beginGroup("UI");
    sets->setValue("enter_key",gs->enter_key);
    sets->setValue("md_fix",gs->md_fix);
    sets->setValue("save_prompt",gs->save_prompt);
    sets->setValue("clear_log",gs->clear_log);
    sets->setValue("server",gs->server);
    sets->setValue("use_server",gs->use_server);
    sets->setValue("use_busybox",gs->use_busybox);
    sets->setValue("chat_font",gs->log_fnt.toString());
    sets->setValue("user_font",gs->usr_fnt.toString());
    sets->setValue("use_attprefix",gs->use_attprefix);
    sets->setValue("mk_dummy",gs->mk_dummy);
    sets->setValue("att_prefix",gs->att_prefix);
    sets->setValue("txt_prefix",gs->txt_prefix);
    sets->setValue("txt_suffix",gs->txt_suffix);

    sets->beginWriteArray("RQP");
    for (int i = 0; i < (int)gs->rqps.size(); i++) {
        sets->setArrayIndex(i);
        sets->setValue("file",gs->rqps.at(i).fn);
        sets->setValue("en",gs->rqps.at(i).enabled);
    }
    sets->endArray();
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
    p->grp_attn_n = ui->gaFactor->value();
    p->grp_attn_w = ui->gaWidth->value();

    if (ui->ctxRopeScale->isChecked())
        p->grp_attn_n = 1;

    llama_sampling_params* s = &(p->sparams);
    s->top_p = ui->topP->value();
    s->top_k = ui->topK->value();
    s->tfs_z = ui->tailFree->value();
    s->typical_p = ui->typP->value();
    s->mirostat_tau = ui->Entropy->value();
    s->mirostat_eta = ui->learnRate->value();
    s->temp = ui->tempEdit->value();
    s->penalty_last_n = ui->lastN->value();
    s->penalty_repeat = ui->repPenalty->value();

    s->mirostat = 0;
    if (ui->sampMiro1->isChecked() || ui->sampMiro2->isChecked())
        s->mirostat = ui->sampMiro1->isChecked()? 1:2;
    else if (ui->sampGreedy->isChecked())
        s->temp = -1;

    AnnaGuiSettings* gs = (AnnaGuiSettings*)pconfig->user;
    gs->enter_key = ui->enterButn->currentIndex();
    gs->md_fix = ui->fixMD->isChecked();
    gs->save_prompt = ui->savePrompt->isChecked();
    gs->clear_log = ui->clearLog->isChecked();
    gs->server = ui->serverAddr->text();
    gs->use_server = ui->useServer->isChecked();
    gs->use_busybox = ui->useBusyBox->isChecked();
    gs->log_fnt = ui->chatLogExample->font();
    gs->usr_fnt = ui->userInExample->font();
    gs->use_attprefix = ui->aaUsePrefix->isChecked();
    gs->mk_dummy = ui->mkDummy->isChecked();
    gs->att_prefix = ui->aaPrefix->text();
    gs->txt_prefix = ui->taPrefix->text();
    gs->txt_suffix = ui->taSuffix->text();

    gs->rqps.clear();
    for (int i = 0; i < ui->rqpList->count(); i++) {
        AnnaRQPFile r;
        r.fn = ui->rqpList->item(i)->text();
        r.enabled = ui->rqpList->item(i)->checkState();
        gs->rqps.push_back(r);
    }
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
    p->grp_attn_n = sets->value("ga_factor",p->grp_attn_n).toInt();
    p->grp_attn_w = sets->value("ga_width",p->grp_attn_w).toInt();

    llama_sampling_params* s = &(p->sparams);
    sets->endGroup();
    sets->beginGroup("Sampling");
    s->top_p = sets->value("topp",s->top_p).toFloat();
    s->top_k = sets->value("topk",s->top_k).toInt();
    s->tfs_z = sets->value("tfs",s->tfs_z).toFloat();
    s->typical_p = sets->value("typp",s->typical_p).toFloat();
    s->mirostat_tau = sets->value("mtau",s->mirostat_tau).toFloat();
    s->mirostat_eta = sets->value("meta",s->mirostat_eta).toFloat();
    s->temp = sets->value("temp",s->temp).toFloat();
    s->penalty_last_n = sets->value("rep_last_n",s->penalty_last_n).toInt();
    s->penalty_repeat = sets->value("rep_penalty",s->penalty_repeat).toFloat();
    s->mirostat = sets->value("mirostat",s->mirostat).toInt();

    AnnaGuiSettings* gs = (AnnaGuiSettings*)cfg->user;
    sets->endGroup();
    sets->beginGroup("UI");
    gs->enter_key = sets->value("enter_key",gs->enter_key).toInt();
    gs->md_fix = sets->value("md_fix",gs->md_fix).toBool();
    gs->save_prompt = sets->value("save_prompt",gs->save_prompt).toBool();
    gs->clear_log = sets->value("clear_log",gs->clear_log).toBool();
    gs->server = sets->value("server",gs->server).toString();
    gs->use_server = sets->value("use_server",gs->use_server).toBool();
    gs->use_busybox = sets->value("use_busybox",gs->use_busybox).toBool();
    gs->log_fnt = LoadFont(sets,"chat_font",gs->log_fnt);
    gs->usr_fnt = LoadFont(sets,"user_font",gs->usr_fnt);
    gs->use_attprefix = sets->value("use_attprefix",gs->use_attprefix).toBool();
    gs->mk_dummy = sets->value("mk_dummy",gs->mk_dummy).toBool();
    gs->att_prefix = sets->value("att_prefix",gs->att_prefix).toString();
    gs->txt_prefix = sets->value("txt_prefix",gs->txt_prefix).toString();
    gs->txt_suffix = sets->value("txt_suffix",gs->txt_suffix).toString();

    int n_rqps = sets->beginReadArray("RQP");
    gs->rqps.clear();
    for (int i = 0; i < n_rqps; i++) {
        AnnaRQPFile r;
        sets->setArrayIndex(i);
        r.fn = sets->value("file").toString();
        r.enabled = sets->value("en").toBool();
        gs->rqps.push_back(r);
    }
    sets->endArray();
}

QString SettingsDialog::GetSaveFileName(QString title, QString filter, QString ext)
{
    QString fn = QFileDialog::getSaveFileName(this,title,"",filter);
    if (!fn.isEmpty()) {
        QFileInfo fi(fn);
        if (fi.suffix().isEmpty()) fn += ext;
    }
    return fn;
}

void SettingsDialog::on_tempKnob_valueChanged(int value)
{
    ui->tempEdit->setValue(value / 100.f);
}

void SettingsDialog::on_tempEdit_valueChanged(double arg1)
{
    ui->tempKnob->setValue(floor(arg1 * 100.f));
}

void SettingsDialog::on_pushButton_clicked()
{
    bool ok;
    QFont f = QFontDialog::getFont(&ok,ui->chatLogExample->font(),this,"Select font for the Chat Log");
    if (ok) ui->chatLogExample->setFont(f);
}

void SettingsDialog::on_pushButton_2_clicked()
{
    bool ok;
    QFont f = QFontDialog::getFont(&ok,ui->userInExample->font(),this,"Select font for the user input box");
    if (ok) ui->userInExample->setFont(f);
}

QFont SettingsDialog::LoadFont(QSettings* sets, QString prefix, const QFont& prev)
{
    QFont fnt(prev);
    QString s = sets->value(prefix,QString()).toString();
    if (!s.isEmpty()) fnt.fromString(s);
    return fnt;
}

void SettingsDialog::on_gaFactor_editingFinished()
{
    int rem = (int)ui->gaWidth->value() % ui->gaFactor->value();
    if (rem > 0) ui->gaWidth->setValue(ui->gaWidth->value()-rem);
}

void SettingsDialog::on_gaWidth_editingFinished()
{
    on_gaFactor_editingFinished();
}

void SettingsDialog::on_gaFactor_valueChanged(int arg1)
{
    ui->gaWidth->setSingleStep(arg1);
}

void SettingsDialog::on_pushButton_3_clicked()
{
    QString fn = GetSaveFileName("Create new RQP file","RQP files (*.ini)",".ini");
    if (fn.isEmpty()) return;

    QSettings sts(fn,QSettings::IniFormat);
    sts.beginGroup("MAIN");
    sts.setValue("created",true); // just to create a new INI file
    addRQPfile(fn,true);
}

void SettingsDialog::on_pushButton_4_clicked()
{
    QString fn = QFileDialog::getOpenFileName(this,"Add existing RQP file","","RQP files (*.ini)");
    if (!fn.isEmpty()) addRQPfile(fn,true);
}

void SettingsDialog::on_pushButton_5_clicked()
{
    for (int i = 0; i < ui->rqpList->count(); i++)
        ui->rqpList->item(i)->setCheckState(Qt::Checked);
}

void SettingsDialog::on_pushButton_6_clicked()
{
    for (int i = 0; i < ui->rqpList->count(); i++)
        ui->rqpList->item(i)->setCheckState((ui->rqpList->item(i)->checkState() == Qt::Checked)? Qt::Unchecked : Qt::Checked);
}

void SettingsDialog::on_pushButton_7_clicked()
{
    if (ui->rqpList->currentRow() < 0 || ui->rqpList->currentRow() >= ui->rqpList->count()) return;

    RQPEditor ed;
    ed.filename = ui->rqpList->item(ui->rqpList->currentRow())->text();

    // create an "undo" point
    QFile file(ed.filename);
    if (!file.open(QIODevice::ReadOnly)) return;
    QByteArray undo = file.readAll();
    file.close();

    // spawn RQP editor and restore undo point if editing was cancelled
    if (ed.exec() != QDialog::Accepted) {
        file.open(QIODevice::WriteOnly);
        file.write(undo);
        file.close();
    }
}

void SettingsDialog::addRQPfile(QString fn, bool checked)
{
    ui->rqpList->addItem(fn);
    int i = ui->rqpList->count() - 1;
    Qt::ItemFlags f = ui->rqpList->item(i)->flags() | Qt::ItemIsUserCheckable;
    ui->rqpList->item(i)->setFlags(f);
    ui->rqpList->item(i)->setCheckState(checked? Qt::Checked : Qt::Unchecked);
    ui->rqpList->sortItems();
}

void SettingsDialog::on_pushButton_8_clicked()
{
    if (ui->rqpList->currentRow() < 0 || ui->rqpList->currentRow() >= ui->rqpList->count()) return;
    auto ptr = ui->rqpList->takeItem(ui->rqpList->currentRow());
    if (ptr) delete ptr;
}
