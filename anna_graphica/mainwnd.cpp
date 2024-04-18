#include <thread>
#include <future>
#include <chrono>
#include <string.h>
#include <unistd.h>
#include "mainwnd.h"
#include "ui_mainwnd.h"
#include "settingsdialog.h"
#include "aboutbox.h"
#include "helpbox.h"
#include "revrqpdialog.h"
#include "lscseditor.h"
#include "mainwnd_tabs.h"

using namespace std;

MainWnd::MainWnd(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWnd)
    , brain(nullptr)
{
    ui->setupUi(this);
    seed_label = new QLabel(this);
    busy_box = new BusyBox(nullptr);

    ui->statusbar->addPermanentWidget(seed_label);
    ui->UserInput->installEventFilter(this);
    ui->statusbar->installEventFilter(this);
    ui->menubar->installEventFilter(this);
    ui->AttachmentsList->setIconSize(QSize(AG_ICON_W,AG_ICON_H));
    ui->AINameBox->completer()->setCaseSensitivity(Qt::CaseSensitive);
    ui->UserNameBox->completer()->setCaseSensitivity(Qt::CaseSensitive);

    block = 0;
    on_actionSimple_view_triggered();
    DefaultConfig();
    LoadSettings();
    UpdateRQPs();
    next_attach = nullptr;
    last_username = false;
    nowait = false;
    tokens_cnt = 0;

    ui->statusbar->showMessage("ANNA ver. " ANNA_VERSION " GUI ver. " AG_VERSION " NC ver. " ANNA_CLIENT_VERSION);
}

MainWnd::~MainWnd()
{
    nowait = true;
    SaveSettings();
    if (brain) delete brain;
    guiconfig.rqps.clear();
    UpdateRQPs(); // effectively destroys all QSettings() objects
    delete busy_box;
    delete seed_label;
    delete ui;
}

void MainWnd::DefaultConfig()
{
    config.convert_eos_to_nl = true;
    config.nl_to_turnover = false;
    config.no_pad_in_prefix = false;
    config.verbose_level = AG_VERBOSE;

    config.user = &guiconfig;
    guiconfig.enter_key = 0;
    guiconfig.auto_gpu = false;
    guiconfig.full_reload = false;
    guiconfig.md_fix = true;
    guiconfig.save_prompt = false;
    guiconfig.clear_log = true;
    guiconfig.server = AG_DEFAULT_SERVER;
    guiconfig.use_server = false;
    guiconfig.use_busybox = true;
    guiconfig.use_attprefix = false;
    guiconfig.mk_dummy = false;
    guiconfig.log_fnt = ui->ChatLog->font();
    guiconfig.usr_fnt = ui->UserInput->font();
    guiconfig.use_lscs = false;
}

void MainWnd::LoadSettings()
{
    QString ini = qApp->applicationDirPath() + "/" + AG_CONFIG_FILE;
    if (!QFileInfo::exists(ini)) {
        qDebug("Settings file was not found at '%s'\n",ini.toStdString().c_str());
        return;
    }

    QSettings s(ini,QSettings::IniFormat);
    QStringList keys = s.allKeys();
    if (s.status() != QSettings::NoError || keys.length() < 2) {
        QMessageBox::warning(this,"ANNA","Unable to load settings file! Possible syntax error.\nDefault settings will be used.");
        return;
    }

    // load main settings
    SettingsDialog::LoadSettings(&config,&s);

    // load UI states and positions
    s.endGroup();
    s.beginGroup("Window");
    if (keys.contains("Window/mode")) {
        switch (s.value("mode").toInt()) {
        case 1: on_actionAdvanced_view_triggered(); break;
        case 2: on_actionProfessional_view_triggered(); break;
        }
    }
    if (keys.contains("Window/geom"))
        restoreGeometry(s.value("geom").toByteArray());
    if (keys.contains("Window/state"))
        restoreState(s.value("state").toByteArray());
    if (keys.contains("Window/split"))
        ui->splitter->restoreState(s.value("split").toByteArray());

    LoadComboBox(&s,"ai_name",ui->AINameBox);
    LoadComboBox(&s,"user_name",ui->UserNameBox);

    ui->ChatLog->setFont(guiconfig.log_fnt);
    ui->UserInput->setFont(guiconfig.usr_fnt);

    // load file paths
    s.endGroup();
    s.beginGroup("Files");
    for (int i = 0; i < ANNA_NUM_FILETYPES; i++)
        filedlg_cache[i] = s.value(QString(filetype_names[i]).replace(' ','_'),filedlg_cache[i]).toString();
}

void MainWnd::SaveSettings()
{
    QString ini = qApp->applicationDirPath() + "/" + AG_CONFIG_FILE;
    QSettings s(ini,QSettings::IniFormat);

    // save main settings
    SettingsDialog::SaveSettings(&config,&s);

    // save UI states and positions
    s.endGroup();
    s.beginGroup("Window");
    s.setValue("mode",mode);
    s.setValue("geom",saveGeometry());
    s.setValue("state",saveState());
    s.setValue("split",ui->splitter->saveState());
    SaveComboBox(&s,"ai_name",ui->AINameBox);
    SaveComboBox(&s,"user_name",ui->UserNameBox);

    // save file paths
    s.endGroup();
    s.beginGroup("Files");
    for (int i = 0; i < ANNA_NUM_FILETYPES; i++)
        s.setValue(QString(filetype_names[i]).replace(' ','_'),filedlg_cache[i]);

    qDebug("Settings saved to '%s'\n",ini.toStdString().c_str());
}

bool MainWnd::LoadFile(const QString& fn, QString& str)
{
    QFile file(fn);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    str = in.readAll();
    return true;
}

bool MainWnd::SaveFile(const QString& fn, const QString& str)
{
    QFile file(fn);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << str;
    return true;
}

bool MainWnd::NewBrain()
{
    if (guiconfig.use_server) {
        // create network client instance
        AnnaClient* ptr = new AnnaClient(&config,guiconfig.server.toStdString(),guiconfig.mk_dummy,
                                         [this](int prog, bool wait, auto txt) -> bool {
            return WaitingFun(prog,wait,txt);
        });
        brain = dynamic_cast<AnnaBrain*>(ptr);

    } else if (guiconfig.use_lscs) {
        // create AnnaLSCS-based instance
        AnnaLSCS* ptr = new AnnaLSCS(config.params.model);
        brain = dynamic_cast<AnnaBrain*>(ptr);

    } else {
        // create normal brain
        brain = new AnnaBrain(&config);
    }

    // re-initialize related state variables
    last_username = false;
    tokens_cnt = 0;

    // if all is good, we can exit
    if (brain->getState() == ANNA_READY) return true;

    // error handling
    ui->statusbar->showMessage("Unable to load LLM file: "+QString::fromStdString(brain->getError()));
    delete brain;
    brain = nullptr;
    block = 0; // nothing to block anymore
    return false;
}

void MainWnd::LoadLLM(const QString &fn)
{
    if (block) return;

    // Create new Brain
    if (brain) {
        // delete old brain first
        // TODO: add confirmation dialog if there's an unsaved state/conversation
        delete brain;
        brain = nullptr;
    }

    // Actual loading
    ++block;
    ui->statusbar->showMessage("Loading LLM file... Please wait!");
    qApp->processEvents();
    strncpy(config.params.model,fn.toStdString().c_str(),sizeof(config.params.model)-1);
    if (!NewBrain()) return;
    ui->statusbar->showMessage("LLM file loaded. Please wait for system prompt processing...");
    qApp->processEvents();
    --block;

    // Process initial prompt
    ProcessInput(config.params.prompt);

    // Display whatever initial output we might have already
    cur_chat += QString::fromStdString(brain->getOutput()) + "\n";
    UpdateChatLogFrom(cur_chat);

    // Display the seed value
    uint32_t seed = brain->getConfig().params.seed;
    qDebug("Seed = %u\n",seed);
    seed_label->setText(QString("Seed: %1").arg((int)seed));

    if (brain->getState() != ANNA_ERROR) ui->statusbar->showMessage("Brain is ready");
}

void MainWnd::ForceAIName(const QString &nm)
{
    if (!brain) return;

    brain->setPrefix(string()); // erase any existing prefix first, to prevent accumulation
    if (nm.isEmpty() || nm == "<none>") {
        //qDebug("AI prefix removed");
        return;
    }

    // set actual prefix
    brain->setPrefix(nm.toStdString());
    //qDebug("AI prefix set to %s",nm.toLatin1().data());
}

void MainWnd::ProcessInput(string str)
{
    if (!brain) return;
    ++block; // block out sync-only UI functions

    // detach potentially long process into separate thread
    auto th = async(launch::async,[&] {
        brain->setInput(str);
        while (brain->Processing(true) == ANNA_PROCESSING)
            tokens_cnt = brain->getTokensUsed(); // the GUI will be updated in the main thread
    });

    //use wait function while async object is busy
    while (th.wait_for(AG_STRPROCESS_WAIT) != future_status::ready)
        WaitingFun(0,true,"Processing text...");
    WaitingFun(-1,false);

    // don't forget to indicate any error
    if (brain->getState() == ANNA_ERROR)
        ui->statusbar->showMessage("Error: "+QString::fromStdString(brain->getError()));

    --block; // UI can be used again
}

bool MainWnd::EmbedImage(const QString& fn)
{
    if (!brain) return false;
    ++block; // block out sync-only UI functions

    // detach long process into separate thread
    volatile bool signal = false;
    bool res = false;
    std::thread pt([&]() {
        res = brain->EmbedImage(fn.toStdString());
        signal = true;
    });

    while (!signal) WaitingFun(0,true,"Embedding image...");
    pt.join();
    WaitingFun(-1,false);

    if (!res)
        QMessageBox::critical(this,"ANNA",QString::fromStdString("Unable to embed image: "+brain->getError()));

    --block; // UI can be used again
    return res;
}

bool MainWnd::CheckUsrPrefix(QString& convo)
{
    QString usrbox = ui->UserNameBox->currentText();
    if (!guiconfig.multi_usr) {
        if (convo.endsWith(usrbox)) {
            convo.chop(usrbox.length());
            return true;
        } else
            return false;
    }

    QStringList pfx = usrbox.split('|');
    for (auto &i : pfx) {
        if (convo.endsWith(i)) {
            convo.chop(i.length());
            return true;
        }
    }
    return false;
}

void MainWnd::Generate()
{
    AnnaState s = ANNA_NOT_INITIALIZED;
    string str;
    QString convo;
    stop = false;
    ++block;

    // main LLM generation loop - continues until brain is deleted (in processEvents()), or turnover, or error
    while (brain && !stop) {
        s = brain->Processing(ui->SamplingCheck->isChecked());
        //qDebug("s = %s",AnnaBrain::StateToStr(s).c_str());

        switch (s) {
        case ANNA_READY:
            if (ui->SamplingCheck->isChecked()) {
                s = ANNA_TURNOVER;
                break;
            }
            // fall-thru
        case ANNA_TURNOVER:
            str = brain->getOutput();
            convo += QString::fromStdString(str);
            if (CheckUsrPrefix(convo)) {
                last_username = true;
                s = ANNA_TURNOVER;
            }
            break;
        case ANNA_PROCESSING:
            // nothing to do, waiting
            break;
        case ANNA_ERROR:
            ui->statusbar->showMessage("Error: " + QString::fromStdString(brain->getError()));
            stop = true;
            break;
        default:
            ui->statusbar->showMessage("Wrong brain state: " + QString::fromStdString(AnnaBrain::StateToStr(s)));
            stop = true;
        }

        // set a temporary UI state to interactively "stream" the tokens which are being generated
        QString curout = cur_chat + convo;
        UpdateChatLogFrom(curout);
        ui->ContextFull->setMaximum(config.params.n_ctx);
        tokens_cnt = brain->getTokensUsed();
        ui->ContextFull->setValue(tokens_cnt);
        ui->statusbar->showMessage("Brain state: thinking...");

        // now we can check the RQPs, execute stuff and inject things into LLM, all in this one convenient call
        CheckRQPs(raw_output + convo);

        // now it's a good time to bail if turnover was detected
        if (s == ANNA_TURNOVER) break;
        nowait = true; // this is needed after the first full loop to prevent the brain to fire a wait on every token

        // make it interactive - update the UI
        qApp->processEvents();
    }

    // restore the global state and update the text widgets
    nowait = false;
    --block;
    cur_chat += convo + "\n";
    raw_output += convo + "\n";
    ui->statusbar->showMessage("Brain state: " + QString::fromStdString(AnnaBrain::StateToStr(s)));
    UpdateChatLogFrom(cur_chat);
}

QString MainWnd::GetSaveFileName(const AnnaFileDialogType tp)
{
    if (tp >= ANNA_NUM_FILETYPES) return QString();
    QString title = "Save as " + QString(filetype_names[tp]);
    QString fn = QFileDialog::getSaveFileName(this,title,filedlg_cache[tp],filetype_filters[tp]);
    if (fn.isEmpty()) return fn;
    QFileInfo fi(fn);
    if (fi.suffix().isEmpty()) fn += filetype_defaults[tp];
    filedlg_cache[tp] = fn;
    return fn;
}

QString MainWnd::GetOpenFileName(const AnnaFileDialogType tp)
{
    if (tp >= ANNA_NUM_FILETYPES) return QString();
    QString title = "Open " + QString(filetype_names[tp]) + " file";
    QString fn = QFileDialog::getOpenFileName(this,title,filedlg_cache[tp],filetype_filters[tp]);
    if (fn.isEmpty()) return fn;
    filedlg_cache[tp] = fn;
    return fn;
}

void MainWnd::on_actionSimple_view_triggered()
{
    mode = 0;
    ui->AINameBox->hide();
    ui->AttachmentsList->hide();
    ui->ContextFull->hide();
    ui->UserNameBox->hide();
    ui->UserInputOptions->hide();
    ui->menuDebug->setEnabled(false);
    seed_label->hide();
}

void MainWnd::on_actionAdvanced_view_triggered()
{
    mode = 1;
    ui->AINameBox->show();
    ui->AttachmentsList->show();
    ui->ContextFull->show();
    ui->UserNameBox->show();
    ui->UserInputOptions->show();
    ui->SamplingCheck->hide();
    ui->NewlineCheck->hide();
    ui->BeforeRadio->hide();
    ui->AfterRadio->hide();
    ui->menuDebug->setEnabled(false);
    seed_label->show();
}

void MainWnd::on_actionProfessional_view_triggered()
{
    mode = 2;
    ui->AINameBox->show();
    ui->AttachmentsList->show();
    ui->ContextFull->show();
    ui->UserNameBox->show();
    ui->UserInputOptions->show();
    ui->SamplingCheck->show();
    ui->NewlineCheck->show();
    ui->BeforeRadio->show();
    ui->AfterRadio->show();
    ui->menuDebug->setEnabled(true);
    seed_label->show();
}

void MainWnd::on_ModelFindButton_clicked()
{
    if (!cur_chat.isEmpty()) {
        auto b = QMessageBox::question(this,"ANNA","You have active dialog. Loading another LLM would reset it.\nDo you want to load another LLM?",
                                       QMessageBox::No | QMessageBox::Yes,QMessageBox::Yes);
        if (b != QMessageBox::Yes) return;
    }

    QString fn = GetOpenFileName(ANNA_FILE_LLM);
    if (fn.isEmpty()) return;
    ui->ModelPath->setText(fn);
    on_ModelPath_returnPressed();
}

void MainWnd::on_ModelPath_returnPressed()
{
    QString fn = ui->ModelPath->text();

    ++block;
    if (!config.params.prompt[0]) {
        auto uq = QMessageBox::question(this,"ANNA","Do you want to open a prompt file?\nIf answered No, a default prompt will be used.",
                                        QMessageBox::No | QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::No);
        strcpy(config.params.prompt,AG_DEFAULT_PROMPT);
        if (uq == QMessageBox::Yes)
            on_actionLoad_initial_prompt_triggered();
        else if (uq == QMessageBox::Cancel) {
            config.params.prompt[0] = 0;
            return;
        }
    }
    qApp->processEvents(); // make sure all dialog boxes are closed and not appeared to be frozen
    --block;

    if (guiconfig.clear_log) {
        ui->ChatLog->clear();
        cur_chat.clear();
        raw_output.clear();

    } else if (!cur_chat.isEmpty()) {
        cur_chat += "### New model loaded: " + fn + "\n\n\n\n";
        UpdateChatLogFrom(cur_chat);
    }

    LoadLLM(fn);
    UpdateRQPs();
}

void MainWnd::on_SendButton_clicked()
{
    if (!brain || block) return;

    QString usr, line, log;
    if (!cur_chat.endsWith("\n")) {
        usr = "\n";
        log = "\n";
    }
    log += "\n**";

    QString usrbox = ui->UserNameBox->currentText();
    if (last_username) {
        usr.clear(); // no need for initial newline
        if (!usrbox.isEmpty()) log += usrbox + " "; // we still need to put user prefix into the log
        last_username = false;

    } else if (!usrbox.isEmpty() && !(guiconfig.multi_usr && usrbox.contains('|')))
        line = usrbox + " ";

    line += ui->UserInput->toPlainText();
    if (guiconfig.md_fix) FixMarkdown(line,md_fix_in_tab);
    usr += line;
    log += line;

    if (!ui->NewlineCheck->isChecked()) {
        // normal behavior
        usr += "\n";
        log += "**\n\n";
        ForceAIName(ui->AINameBox->currentText());
    } else {
        log += "** ";
        ForceAIName(""); // no newline, no enforcing
    }

    // no input -> just generate
    if (ui->UserInput->toPlainText().isEmpty()) {
        Generate();
        return;
    }

    ui->UserInput->clear();

    if (next_attach) {
        if (guiconfig.use_attprefix) {
            // insert pre-attchment prefix if needed
            QString pre = guiconfig.att_prefix;
            pre.replace("\\n","\n");
            pre.replace("%f",next_attach->shrt);
            pre.replace("%p",next_attach->fn);
            qDebug("pre = '%s'\n",pre.toStdString().c_str());
            ProcessInput(pre.toStdString());
        }

        if (ui->AfterRadio->isChecked()) {
            // in after-text attachment scenario, force embeddings and evaluation of the user input first, without any sampling
            ProcessInput(usr.toStdString());
            usr.clear(); // don't duplicate the input
            log += "\n_attached: " + next_attach->shrt + "_\n";
        } else
            log = " _attached: " + next_attach->shrt + "_\n" + log;

        if (next_attach->txt.isEmpty()) {
            // image attachment
            if (!EmbedImage(next_attach->fn)) return;
        } else {
            // text attachment
            usr += "\n" + guiconfig.txt_prefix + "\n" + next_attach->txt + "\n" + guiconfig.txt_suffix + "\n";
        }

        next_attach = nullptr;
    }

    //qDebug("usr = '%s'\n",usr.toStdString().c_str());
    //qDebug("log = '%s'\n",log.toStdString().c_str());

    //if (guiconfig.md_fix) FixMarkdown(log,md_fix_in_tab);
    cur_chat += log;
    UpdateChatLogFrom(cur_chat);
    ui->statusbar->showMessage("Processing...");

    // update the UI state and send the final form of the input to the brain
    ++block;
    qApp->processEvents();
    brain->setInput(usr.toStdString());
    --block;

    // now just casually generate a response :)
    Generate();
}

void MainWnd::closeEvent(QCloseEvent* event)
{
    auto b = QMessageBox::question(this,"ANNA","Exit ANNA?\n",QMessageBox::No | QMessageBox::Yes,QMessageBox::Yes);
    if (b == QMessageBox::Yes) event->accept();
    else event->ignore();
}

bool MainWnd::eventFilter(QObject* obj, QEvent* event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        bool eat_ret = false;
        bool shift = ke->modifiers().testFlag(Qt::ShiftModifier);
        switch (guiconfig.enter_key) {
        case 0: eat_ret = !shift; break;
        case 1: eat_ret = shift; break;
        }

        if (ke->key() == Qt::Key_Return && eat_ret) {
            //qDebug("Ate key press %d",ke->key());
            on_SendButton_clicked();
            return true;
        }
        return false;
    }
    case QEvent::StatusTip:
    {
        QStatusTipEvent* se = static_cast<QStatusTipEvent*>(event);
        if (se->tip().isEmpty()) {
            //qDebug("Ate empty status tip event");
            return true;
        }
        return false;
    }
    default:
        break;
    }
    // standard event processing
    return QObject::eventFilter(obj, event);
}

void MainWnd::on_AttachButton_clicked()
{
    if (block) return;

    AnnaAttachment a;
    a.fn = GetOpenFileName(ANNA_FILE_ATTACHMENT);
    if (a.fn.isEmpty()) return;

    QIcon ico;
    QFileInfo inf(a.fn);
    a.shrt = inf.fileName();

    if (a.pic.load(a.fn)) {
        // if we can load it as Pixmap, then it's an image
        ico.addPixmap(a.pic.scaled(AG_ICON_W,AG_ICON_H,Qt::KeepAspectRatio),QIcon::Normal,QIcon::On);

    } else {
        // treat it as a text file
        if (!LoadFile(a.fn,a.txt)) {
            // WTF?!
            QMessageBox::critical(this,"ANNA","ERROR: Unable to load specified file!");
            return;
        }
        QPixmap txtdoc;
        txtdoc.load(":/ico/txt_doc.png");
        ico.addPixmap(txtdoc);
    }

    ui->AttachmentsList->addItem(a.shrt);
    a.itm = ui->AttachmentsList->item(ui->AttachmentsList->count()-1);
    a.itm->setIcon(ico);
    attachs.push_back(a);
    next_attach = &attachs.back();
}

void MainWnd::on_actionLoad_model_triggered()
{
    on_ModelFindButton_clicked();
}

void MainWnd::on_actionNew_dialog_triggered()
{
    if (block) return;

    if (!cur_chat.isEmpty()) {
        if (QMessageBox::question(this,"ANNA","Reset dialog?\n",QMessageBox::No | QMessageBox::Yes,QMessageBox::Yes) != QMessageBox::Yes)
            return;
    }

    if (guiconfig.new_reload) {
        LoadLLM(ui->ModelPath->text());
        if (brain && brain->getState() != ANNA_ERROR)
            ui->statusbar->showMessage("Brain has been reloaded and ready for new dialog.");

    } else if (brain) {
        brain->Reset();
        ui->statusbar->showMessage("Brain reset complete. Please wait for prompt processing...");
        qApp->processEvents();

        ProcessInput(config.params.prompt);
        if (brain->getState() != ANNA_ERROR)
            ui->statusbar->showMessage("Brain has been reset and ready for new dialog.");
    }

    if (guiconfig.clear_log) {
        ui->ChatLog->clear();
        cur_chat.clear();
    } else if (!cur_chat.isEmpty()) {
        cur_chat += "### New chat started\n\n\n\n";
        UpdateChatLogFrom(cur_chat);
    }

    UpdateRQPs();
    raw_output.clear();
    ui->UserInput->clear();
    last_username = false;
    next_attach = nullptr;
}

void MainWnd::on_actionQuit_triggered()
{
    close();
}

void MainWnd::on_actionMarkdown_triggered()
{
    QString fn = GetSaveFileName(ANNA_FILE_DLG_MD);
    if (fn.isEmpty()) return;
    if (guiconfig.save_prompt) on_actionShow_prompt_triggered();
    if (SaveFile(fn,cur_chat))
        ui->statusbar->showMessage("Chat log saved as markdown document.");
    else
        ui->statusbar->showMessage("Unable to write output file.");
}

void MainWnd::on_actionLoad_initial_prompt_triggered()
{
    QString fn = GetOpenFileName(ANNA_FILE_PROMPT);
    QString np;
    if (fn.isEmpty() || !LoadFile(fn,np)) return;
    strncpy(config.params.prompt,np.toStdString().c_str(),sizeof(config.params.prompt)-1);
}

void MainWnd::on_actionSettings_triggered()
{
    SettingsDialog sdlg;
    sdlg.pconfig = &config;

    if (sdlg.exec() == QDialog::Accepted) {
        // update things which don't require any LLM
        ui->ChatLog->setFont(guiconfig.log_fnt);
        ui->UserInput->setFont(guiconfig.usr_fnt);
        UpdateRQPs();
        SaveSettings(); // make sure the file is updated as well

        if (brain) {
            // update things which can be updated on the fly
            brain->setConfig(config);

            // warn the user about others
            if (!cur_chat.isEmpty()) {
                if (QMessageBox::information(this,"ANNA","Most of the settings require the model to be reloaded.\n"
                                             "Do you want to reload the model? The current dialog will be lost.",
                                             QMessageBox::No | QMessageBox::Yes,QMessageBox::No) == QMessageBox::Yes)
                    LoadLLM(QString::fromStdString(config.params.model));
            }
        }
    }
}

void MainWnd::on_actionClear_attachments_triggered()
{
    ui->AttachmentsList->clear();
    attachs.clear();
}

void MainWnd::on_actionLoad_vision_encoder_triggered()
{
    if (!brain) {
        QMessageBox::warning(this,"ANNA","You need to load the language model first.");
        return;
    }

    QString fn = GetOpenFileName(ANNA_FILE_CLIP);
    if (fn.isEmpty()) return;
    brain->setClipModelFile(fn.toStdString());
    ui->statusbar->showMessage("CLiP model file set to "+fn);
}

void MainWnd::on_actionPlain_text_triggered()
{
    QString fn = GetSaveFileName(ANNA_FILE_DLG_TXT);
    if (fn.isEmpty()) return;
    if (guiconfig.save_prompt) on_actionShow_prompt_triggered();
    if (SaveFile(fn,ui->ChatLog->toPlainText()))
        ui->statusbar->showMessage("Chat log saved as plain text document.");
    else
        ui->statusbar->showMessage("Unable to write output file.");
}

void MainWnd::on_actionAbout_triggered()
{
    AboutBox box;
    box.exec();
}

void MainWnd::on_stopButton_clicked()
{
    stop = true;
    ui->statusbar->showMessage("Brain: stopped");
}

void MainWnd::on_actionHTML_triggered()
{
    QString fn = GetSaveFileName(ANNA_FILE_DLG_HTML);
    if (fn.isEmpty()) return;
    if (guiconfig.save_prompt) on_actionShow_prompt_triggered();
    if (SaveFile(fn,ui->ChatLog->toHtml()))
        ui->statusbar->showMessage("Chat log saved as HTML document.");
    else
        ui->statusbar->showMessage("Unable to write output file.");
}

void MainWnd::on_actionSave_state_triggered()
{
    if (!brain) return;
    QString fn = GetSaveFileName(ANNA_FILE_MODEL_STATE);
    if (fn.isEmpty()) return;
    string dlg = cur_chat.toStdString();
    if (brain->SaveState(fn.toStdString(),(void*)dlg.data(),dlg.size()))
        ui->statusbar->showMessage("Model state has been saved to "+fn);
    else
        ui->statusbar->showMessage("Error: "+QString::fromStdString(brain->getError()));
}

bool MainWnd::LoadLLMState(const QString& fn)
{
    if (block) return false;

    // if full reload required, let's remove old brain first
    if (brain && guiconfig.full_reload) {
        delete brain;
        brain = nullptr;
    }

    // shortcut for loading model + state in an easier way
    if (!brain) {
        AnnaBrain br; // an "empty" brain (no model loaded)
        if (br.LoadState(fn.toStdString(),nullptr,nullptr)) {
            // now we can construct the real brain with the config extracted
            ++block;
            ui->statusbar->showMessage("Loading LLM file... Please wait!");
            qApp->processEvents();

            config = br.getConfig();
            config.user = &guiconfig;
            if (!NewBrain()) return false;

            ui->ModelPath->setText(config.params.model);
            ui->statusbar->showMessage("LLM file loaded. Loading state...");
            qApp->processEvents();
            --block;

        } else {
            ui->statusbar->showMessage("Error: "+QString::fromStdString(br.getError()));
            return false;
        }
    }

    // normal stuff now - load the state using existing model
    string dlg(AG_MAXTEXT,'\0');
    size_t usrlen = AG_MAXTEXT;
    if (!brain->LoadState(fn.toStdString(),(void*)dlg.data(),&usrlen)) {
        ui->statusbar->showMessage("Error: "+QString::fromStdString(brain->getError()));
        return false;
    }

    dlg.resize(usrlen);
    cur_chat = QString::fromStdString(dlg);
    UpdateChatLogFrom(cur_chat);
    raw_output.clear();
    UpdateRQPs();

    return true;
}

void MainWnd::on_actionLoad_state_triggered()
{
    QString fn = GetOpenFileName(ANNA_FILE_MODEL_STATE);
    if (fn.isEmpty()) return;
    if (LoadLLMState(fn))
        ui->statusbar->showMessage("Model state has been loaded from "+fn);
}

void MainWnd::on_actionShow_prompt_triggered()
{
    QString r = QString(config.params.prompt);
    if (guiconfig.md_fix) FixMarkdown(r,md_fix_in_tab);
    if (!cur_chat.startsWith(r)) {
        cur_chat = r + "\n\n" + cur_chat;
        UpdateChatLogFrom(cur_chat);
    }
}

void MainWnd::FixMarkdown(QString& s, const char** tab)
{
    for (int i = 0; tab[i]; i+=2) {
        //qDebug("Filter %d before: '%s'\n",i,s.toStdString().c_str());
        QRegExp ex(tab[i]);
        for (int n = 0; n < AG_MDFIX_FAILSAFE && ex.indexIn(s) != -1; n++) {
            //qDebug("%d %d %d\n",i,ex.indexIn(s),ex.matchedLength());
            s.replace(ex,tab[i+1]);
            if (s.length() > AG_MAXTEXT) break;
        }
        //qDebug("Filter %d after: '%s'\n",i,s.toStdString().c_str());
    }
}

void MainWnd::UpdateRQPs()
{
    for (auto & i : rqps) {
        if (i.s) delete i.s;
    }
    rqps.clear();

    for (auto & i : guiconfig.rqps) {
        if (!i.enabled) continue;
        AnnaRQPState s;
        s.fsm = 0;
        s.lpos = 0;
        s.s = new QSettings(i.fn,QSettings::IniFormat);
        rqps.push_back(s);
    }
}

void MainWnd::CheckRQPs(const QString& inp)
{
    ++block; // lock out the UI while processing RQPs
    for (auto & i : rqps) {
        QString out = RQPEditor::DoRequest(i,inp,true,[&](QString& fn, QStringList& args) -> bool {
            if (!ui->actionReview_RQP->isChecked()) return true;
            RevRQPDialog dlg;
            dlg.setCode(fn,args);
            if (dlg.exec() == QDialog::Accepted) {
                dlg.getCode(fn,args);
                return true;
            } else
                return false;
        },[&](QString& fn) {
            ui->statusbar->showMessage("Running external command "+fn);
        });

        if (!out.isEmpty()) {
            ui->statusbar->showMessage("Finished running external command");
            ProcessInput(out.toStdString());
            if (ui->actionShow_RQP_output->isChecked()) {
                FixMarkdown(out,md_fix_in_tab);
                cur_chat += "\n\n### RQP output:\n\n" + out + "\n\n### End of RQP output\n\n";
                UpdateChatLogFrom(cur_chat);
            }
        }
    }
    --block;
}

void MainWnd::on_AttachmentsList_itemDoubleClicked(QListWidgetItem *item)
{
    auto it = find_if(attachs.begin(),attachs.end(),[item] (auto & o) { return o.itm == item; });
    if (it != attachs.end()) {
        next_attach = &(*it);
        ui->statusbar->showMessage(it->shrt + " will be attached to your message");
    }
}

void MainWnd::on_actionRefresh_chat_box_triggered()
{
    UpdateChatLogFrom(cur_chat);
}

void MainWnd::UpdateChatLogFrom(QString s)
{
    FixMarkdown(s,md_fix_out_tab);
    ui->ChatLog->setMarkdown(s);
    ui->ChatLog->moveCursor(QTextCursor::End);
    ui->ChatLog->ensureCursorVisible();
}

void MainWnd::on_actionShow_context_tokens_triggered()
{
    if (brain) ui->ChatLog->setPlainText(QString::fromStdString(brain->PrintContext()));
}

void MainWnd::on_actionQuick_save_triggered()
{
    if (!brain) return;
    QString fn = qApp->applicationDirPath() + "/" + AG_QUICK_FILE;
    string dlg = cur_chat.toStdString();
    if (brain->SaveState(fn.toStdString(),dlg.data(),dlg.size()))
        ui->statusbar->showMessage("Quicksave point saved");
    else
        ui->statusbar->showMessage("Error: "+QString::fromStdString(brain->getError()));
}

void MainWnd::on_actionQuick_load_triggered()
{
    QString fn = qApp->applicationDirPath() + "/" + AG_QUICK_FILE;
    if (LoadLLMState(fn))
        ui->statusbar->showMessage("Quickload complete");
}

void MainWnd::SaveComboBox(QSettings* sets, QString prefix, QComboBox* box)
{
    sets->setValue(prefix+"_default",box->currentText());
    sets->beginWriteArray(prefix);
    for (int i = 0; i < box->count(); i++) {
        sets->setArrayIndex(i);
        sets->setValue("s",box->itemText(i));
    }
    sets->endArray();
}

void MainWnd::LoadComboBox(QSettings* sets, QString prefix, QComboBox* box)
{
    int n = sets->beginReadArray(prefix);
    if (n > 0) box->clear();
    for (int i = 0; i < n; i++) {
        sets->setArrayIndex(i);
        box->addItem(sets->value("s").toString());
    }
    sets->endArray();
    box->setCurrentText(sets->value(prefix+"_default").toString());
}

void MainWnd::on_actionOffline_help_triggered()
{
    HelpBox hb;
    hb.exec();
}

void MainWnd::on_actionClear_chat_log_triggered()
{
    cur_chat.clear();
    raw_output.clear();
    ui->ChatLog->clear();
}

void MainWnd::on_actionUse_current_input_as_prompt_triggered()
{
    QString in = ui->UserInput->toPlainText();
    ui->UserInput->clear();
    strncpy(config.params.prompt,in.toStdString().c_str(),sizeof(config.params.prompt)-1);
}

void MainWnd::on_actionReset_prompt_to_default_triggered()
{
    strcpy(config.params.prompt,AG_DEFAULT_PROMPT);
}

bool MainWnd::WaitingFun(int prog, bool wait, const string& text)
{
    if (nowait) return false; // is waiting temporarily prohibited?
    if (!busybox_lock.try_lock()) return false; // this might signal to other threads that waiting is already happening
    ++block; // block the UI

    if (prog >= 0) {
        // Some process is happening
        ui->statusbar->showMessage(QString::fromStdString(text));

        // this cycle allows for a more "interactive" waiting with multiple calls to processEvents()
        for (int i = 0; i < AG_SERVER_WAIT_CYCLES; i++) {
            if (guiconfig.use_busybox) busy_box->Use(geometry(),prog);
            qApp->processEvents();
            if (wait) usleep(1000UL * AG_SERVER_WAIT_MS);
            else break; // no need to do many cycles, as we're not waiting
        }

    } else {
        // Process has finished
        if (guiconfig.use_busybox) busy_box->close();
    }

    // keep UI updated with asynchronously received values
    ui->ContextFull->setMaximum(config.params.n_ctx);
    ui->ContextFull->setValue(tokens_cnt);

    // unlock and move along
    --block;
    busybox_lock.unlock();
    return true;
}

void MainWnd::on_actionUndo_triggered()
{
    if (block || !brain) return;
    brain->Undo();
    // TODO: revert chat log as well
}

void MainWnd::on_actionStop_triggered()
{
    on_stopButton_clicked();
}

void MainWnd::on_actionContinue_triggered()
{
    ForceAIName("");
    ui->SamplingCheck->setChecked(false);
    Generate();
}

void MainWnd::on_actionRequester_plugins_triggered()
{
    int old = SettingsDialog::tab_idx;
    SettingsDialog::tab_idx = AG_SETS_RQP_TAB;
    on_actionSettings_triggered();
    SettingsDialog::tab_idx = old;
}

void MainWnd::on_actionShow_lock_triggered()
{
    ui->statusbar->showMessage(QString::fromStdString(AnnaBrain::myformat("lock value = %d",int(block))));
}

void MainWnd::on_actionLSCS_editor_triggered()
{
    LSCSEditor ed;
    while (!ed.exec()) ; // work around Qt's stupid hardcoded bind for Esc key
    qDebug("LSCS editor returned\n");
}
