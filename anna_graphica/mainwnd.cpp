#include <string.h>
#include "mainwnd.h"
#include "ui_mainwnd.h"
#include "settingsdialog.h"
#include "aboutbox.h"
#include "helpbox.h"

static const char* filetype_names[ANNA_NUM_FILETYPES] = {
    "LLM",
    "prompt",
    "dialog as text",
    "dialog as MD",
    "dialog as HTML",
    "model state",
    "CLiP encoder",
    "attachment"
};

static const char* filetype_filters[ANNA_NUM_FILETYPES] = {
    "GGUF files (*.gguf)",
    "Text files (*.txt)",
    "Text files (*.txt)",
    "Markdown files (*.md);;Text files (*.txt)",
    "HTML files (*.html *.htm)",
    "ANNA save states (*.anna);;All files (*.*)",
    "GGUF files (*.gguf)",
    "Text files (*.txt);;Image files (*.png *.jpg *.jpeg *.bmp *.xpm *.ppm *.pbm *.pgm *.xbm *.xpm)",
};

static const char* filetype_defaults[ANNA_NUM_FILETYPES] = {
    ".gguf",
    ".txt",
    ".txt",
    ".md",
    ".html",
    ".anna",
    ".gguf",
    ".txt",
};

static const char* md_fix_tab[] = {
    "\\n\\*\\*([^*\\n]+\\s?)\\n", "\n**\\1**\n**",
    "([^\\n])\\n([^\\n])", "\\1\n\n\\2",
    "([^\\\\])#", "\\1\\#",
    "\\n\\*\\*[ \t]+", "\n**",
    "([^\\\\])<", "\\1\\<",
    NULL, NULL // terminator
};

MainWnd::MainWnd(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWnd)
    , brain(nullptr)
{
    ui->setupUi(this);
    seed_label = new QLabel(this);

    ui->statusbar->addPermanentWidget(seed_label);
    ui->UserInput->installEventFilter(this);
    ui->statusbar->installEventFilter(this);
    ui->menubar->installEventFilter(this);
    ui->AttachmentsList->setIconSize(QSize(GUI_ICON_W,GUI_ICON_H));
    ui->AINameBox->completer()->setCaseSensitivity(Qt::CaseSensitive);
    ui->UserNameBox->completer()->setCaseSensitivity(Qt::CaseSensitive);

    block = false;
    on_actionSimple_view_triggered();
    DefaultConfig();
    LoadSettings();
    UpdateRQPs();
    next_attach = nullptr;
    last_username = false;

    ui->statusbar->showMessage("ANNA version " ANNA_VERSION);
}

MainWnd::~MainWnd()
{
    SaveSettings();
    if (brain) delete brain;
    guiconfig.rqps.clear();
    UpdateRQPs(); // effectively destroys all QSettings() objects
    delete seed_label;
    delete ui;
}

void MainWnd::DefaultConfig()
{
    config.convert_eos_to_nl = true;
    config.nl_to_turnover = false;
    config.verbose_level = 1;

    gpt_params* p = &config.params;
    p->seed = 0;
    p->n_threads = std::thread::hardware_concurrency();
    if (p->n_threads < 1) p->n_threads = 1;
    p->n_predict = -1;
    p->n_ctx = ANNA_DEFAULT_CONTEXT;
    p->n_batch = ANNA_DEFAULT_BATCH;
    p->n_gpu_layers = 0;
    p->model[0] = 0;
    p->prompt[0] = 0;
    p->sparams.temp = ANNA_DEFAULT_TEMP;

    config.user = &guiconfig;
    guiconfig.enter_key = 0;
    guiconfig.md_fix = true;
    guiconfig.save_prompt = false;
    guiconfig.clear_log = true;
    guiconfig.server = ANNA_DEFAULT_SERVER;
    guiconfig.use_server = false;
    guiconfig.log_fnt = ui->ChatLog->font();
    guiconfig.usr_fnt = ui->UserInput->font();
}

void MainWnd::LoadSettings()
{
    QString ini = qApp->applicationDirPath() + "/" + ANNA_CONFIG_FILE;
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
    QString ini = qApp->applicationDirPath() + "/" + ANNA_CONFIG_FILE;
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
    brain = guiconfig.use_server? new AnnaClient(&config,guiconfig.server.toStdString()) : new AnnaBrain(&config);
    if (brain->getState() == ANNA_READY) return true;

    ui->statusbar->showMessage("Unable to load LLM file: "+QString::fromStdString(brain->getError()));
    delete brain;
    brain = nullptr;
    block = false; // nothing to block anymore
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
    block = true;
    ui->statusbar->showMessage("Loading LLM file... Please wait!");
    qApp->processEvents();
    strncpy(config.params.model,fn.toStdString().c_str(),sizeof(config.params.model)-1);
    if (!NewBrain()) return;
    ui->statusbar->showMessage("LLM file loaded. Please wait for system prompt processing...");
    qApp->processEvents();
    block = false;

    // Process initial prompt
    ProcessInput(config.params.prompt);
    seed_label->setText(QString("Seed: %1").arg((int)brain->getConfig().params.seed));
    if (brain->getState() != ANNA_ERROR) ui->statusbar->showMessage("Brain is ready");
}

void MainWnd::ForceAIName(const QString &nm)
{
    if (!brain) return;

    brain->setPrefix(std::string()); // erase any existing prefix first, to prevent accumulation
    if (nm.isEmpty() || nm == "<none>") {
        qDebug("AI prefix removed");
        return;
    }

    // set actual prefix
    brain->setPrefix(nm.toStdString());
    qDebug("AI prefix set to %s",nm.toLatin1().data());
}

void MainWnd::ProcessInput(std::string str)
{
    if (!brain) return;

    brain->setInput(str);
    while (brain->Processing(true) == ANNA_PROCESSING) {
        ui->ContextFull->setMaximum(config.params.n_ctx);
        ui->ContextFull->setValue(brain->getTokensUsed());

        // simulate multi-threaded UI, but don't forget to block out sync-only functions
        block = true;
        qApp->processEvents();
        block = false;
    }

    if (brain->getState() == ANNA_ERROR)
        ui->statusbar->showMessage("Error: "+QString::fromStdString(brain->getError()));
}

void MainWnd::Generate()
{
    //bool skips = false;
    std::string str;
    QString convo;
    stop = false;
    block = true;

    while (brain && !stop) {
        AnnaState s = brain->Processing(ui->SamplingCheck->isChecked());
        qDebug("s = %s",AnnaBrain::StateToStr(s).c_str());

        switch (s) {
        case ANNA_READY:
            if (ui->SamplingCheck->isChecked()) return;
            // fall-thru
        case ANNA_TURNOVER:
            str = brain->getOutput();
            qDebug("str = %s\n",str.c_str());
            convo += QString::fromStdString(str);
            if (!ui->UserNameBox->currentText().isEmpty() && convo.endsWith(ui->UserNameBox->currentText())) {
                last_username = true;
                s = ANNA_TURNOVER;
                convo.chop(ui->UserNameBox->currentText().length());
                qDebug("convo = %s\n",convo.toStdString().c_str());
            }
            break;
        case ANNA_ERROR:
            ui->statusbar->showMessage("Error: " + QString::fromStdString(brain->getError()));
            return;
        default:
            break;
        }

        ui->statusbar->showMessage("Brain state: " + QString::fromStdString(AnnaBrain::StateToStr(s)));
        ui->ChatLog->setMarkdown(cur_chat + convo);
        ui->ChatLog->moveCursor(QTextCursor::End);
        ui->ChatLog->ensureCursorVisible();
        ui->ContextFull->setMaximum(config.params.n_ctx);
        ui->ContextFull->setValue(brain->getTokensUsed());

        CheckRQPs(raw_output + convo);
        if (s == ANNA_TURNOVER) break;
        qApp->processEvents();
    }

    block = false;
    cur_chat += convo + "\n";
    raw_output += convo + "\n";
    on_actionRefresh_chat_box_triggered();
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
        auto b = QMessageBox::question(this,"ANNA","You have active dialog. Loading another LLM would reset it.\nDo you want to load another LLM?",QMessageBox::No | QMessageBox::Yes,QMessageBox::Yes);
        if (b != QMessageBox::Yes) return;
    }

    QString fn = GetOpenFileName(ANNA_FILE_LLM);
    if (fn.isEmpty()) return;
    ui->ModelPath->setText(fn);

    if (!config.params.prompt[0]) {
        auto uq = QMessageBox::question(this,"ANNA","Do you want to open a prompt file?\nIf answered No, a default prompt will be used.",
                                        QMessageBox::No | QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::No);
        strcpy(config.params.prompt,ANNA_DEFAULT_PROMPT);
        if (uq == QMessageBox::Yes)
            on_actionLoad_initial_prompt_triggered();
        else if (uq == QMessageBox::Cancel) {
            config.params.prompt[0] = 0;
            return;
        }
    }

    if (guiconfig.clear_log) {
        ui->ChatLog->clear();
        cur_chat.clear();
        raw_output.clear();
    } else if (!cur_chat.isEmpty()) {
        cur_chat += "### New model loaded: " + fn + "\n\n\n\n";
        on_actionRefresh_chat_box_triggered();
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

    if (last_username) {
        usr.clear(); // no need for initial newline
        log += ui->UserNameBox->currentText() + " "; // we still need to put it into the log
        last_username = false;
    } else
        line = ui->UserNameBox->currentText() + " ";

    line += ui->UserInput->toPlainText();
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
        if (ui->AfterRadio->isChecked()) {
            // in after-text attachment scenario, force embeddings and evaluation of the user input first, without any sampling
            ProcessInput(usr.toStdString());
            usr.clear(); // don't duplicate the input
            log += "\n_attached: " + next_attach->shrt + "_\n";
        } else
            log = " _attached: " + next_attach->shrt + "_\n" + log;

        if (next_attach->txt.isEmpty()) {
            // image attachment
            if (!brain->EmbedImage(next_attach->fn.toStdString())) {
                QMessageBox::critical(this,"ANNA",QString::fromStdString("Unable to embed image: "+brain->getError()));
                return;
            }
        } else {
            // text attachment
            usr += next_attach->txt + "\n";
        }

        next_attach = nullptr;
    }

    //qDebug("usr = '%s'\n",usr.toStdString().c_str());
    //qDebug("log = '%s'\n",log.toStdString().c_str());

    if (guiconfig.md_fix) FixMarkdown(log);
    cur_chat += log;
    on_actionRefresh_chat_box_triggered();
    ui->statusbar->showMessage("Processing...");
    block = true;
    qApp->processEvents();

    brain->setInput(usr.toStdString());
    Generate();
    block = false;
}

void MainWnd::closeEvent(QCloseEvent* event)
{
    auto b = QMessageBox::question(this,"ANNA","Exit ANNA?\n",QMessageBox::No | QMessageBox::Yes,QMessageBox::Yes);
    if (b == QMessageBox::Yes) {
        if (brain) {
            // This will stop current processing, as it is synchronous
            delete brain;
            brain = nullptr;
        }
        event->accept();
    } else
        event->ignore();
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
            qDebug("Ate key press %d",ke->key());
            on_SendButton_clicked();
            return true;
        }
        return false;
    }
    case QEvent::StatusTip:
    {
        QStatusTipEvent* se = static_cast<QStatusTipEvent*>(event);
        if (se->tip().isEmpty()) {
            qDebug("Ate empty status tip event");
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
    AnnaAttachment a;
    a.fn = GetOpenFileName(ANNA_FILE_ATTACHMENT);
    if (a.fn.isEmpty()) return;

    QIcon ico;
    QFileInfo inf(a.fn);
    a.shrt = inf.baseName();

    if (a.pic.load(a.fn)) {
        // if we can load it as Pixmap, then it's an image
        ico.addPixmap(a.pic.scaled(GUI_ICON_W,GUI_ICON_H,Qt::KeepAspectRatio),QIcon::Normal,QIcon::On);

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

    if (brain) {
        brain->Reset();
        ui->statusbar->showMessage("Brain reset complete. Please wait for prompt processing...");
        qApp->processEvents();

        ProcessInput(config.params.prompt);
        ui->statusbar->showMessage("Brain has been reset and is now ready.");
    }

    if (guiconfig.clear_log) {
        ui->ChatLog->clear();
        cur_chat.clear();
    } else if (!cur_chat.isEmpty()) {
        cur_chat += "### New chat started\n\n\n\n";
        on_actionRefresh_chat_box_triggered();
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

    if (sdlg.exec() == QDialog::Accepted && brain) {
        // update things which don't require any LLM
        ui->ChatLog->setFont(guiconfig.log_fnt);
        ui->UserInput->setFont(guiconfig.usr_fnt);
        UpdateRQPs();

        if (brain) {
            // update things which can be updated on the fly
            brain->setConfig(config);

            // warn the user about others
            if (!cur_chat.isEmpty()) {
                if (QMessageBox::information(this,"ANNA","Most of the settings require the model to be reloaded.\n"
                                             "Do you want to reload the model? The current dialog will be lost.",QMessageBox::No | QMessageBox::Yes,QMessageBox::No) == QMessageBox::Yes)
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

void MainWnd::on_pushButton_clicked()
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
    std::string dlg = cur_chat.toStdString();
    if (brain->SaveState(fn.toStdString(),(void*)dlg.data(),dlg.size()))
        ui->statusbar->showMessage("Model state has been saved to "+fn);
    else
        ui->statusbar->showMessage("Error: "+QString::fromStdString(brain->getError()));
}

bool MainWnd::LoadLLMState(const QString& fn)
{
    if (block) return false;

    // shortcut for loading model + state in an easier way
    if (!brain) {
        AnnaBrain br; // an "empty" brain (no model loaded)
        if (br.LoadState(fn.toStdString(),nullptr,nullptr)) {
            // now we can construct the real brain with the config extracted
            block = true;
            ui->statusbar->showMessage("Loading LLM file... Please wait!");
            qApp->processEvents();

            config = br.getConfig();
            if (!NewBrain()) return false;

            ui->statusbar->showMessage("LLM file loaded. Loading state...");
            qApp->processEvents();
            block = false;

        } else {
            ui->statusbar->showMessage("Error: "+QString::fromStdString(br.getError()));
            return false;
        }
    }

    // normal stuff now - load the state using existing model
    std::string dlg(GUI_MAXTEXT,'\0');
    size_t usrlen = GUI_MAXTEXT;
    if (!brain->LoadState(fn.toStdString(),(void*)dlg.data(),&usrlen)) {
        ui->statusbar->showMessage("Error: "+QString::fromStdString(brain->getError()));
        return false;
    }

    dlg.resize(usrlen);
    cur_chat = QString::fromStdString(dlg);
    on_actionRefresh_chat_box_triggered();
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
    if (guiconfig.md_fix) FixMarkdown(r);
    if (!cur_chat.startsWith(r)) {
        cur_chat = r + "\n\n" + cur_chat;
        on_actionRefresh_chat_box_triggered();
    }
}

void MainWnd::FixMarkdown(QString& s)
{
    for (int i = 0; md_fix_tab[i]; i+=2) {
        //qDebug("Filter %d before: '%s'\n",i,s.toStdString().c_str());
        QRegExp ex(md_fix_tab[i]);
        for (int n = 0; n < ANNA_MDFIX_FAILSAFE && ex.indexIn(s) != -1; n++) {
            //qDebug("%d %d %d\n",i,ex.indexIn(s),ex.matchedLength());
            s.replace(ex,md_fix_tab[i+1]);
            if (s.length() > GUI_MAXTEXT) break;
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
    bool pre_block = block;
    block = true;
    for (auto & i : rqps) {
        QString out = RQPEditor::DoRequest(i,inp,true,[&](QString& fn) {
            ui->statusbar->showMessage("Running external command "+fn);
        });
        if (!out.isEmpty()) {
            ui->statusbar->showMessage("Finished running external command");
            ProcessInput(out.toStdString());
            if (ui->actionShow_RQP_output->isChecked()) {
                FixMarkdown(out);
                cur_chat += "\n\n### RQP output:\n\n" + out + "\n\n### End of RQP output\n\n";
                on_actionRefresh_chat_box_triggered();
            }
        } else
            ui->statusbar->clearMessage();
    }
    block = pre_block;
}

void MainWnd::on_AttachmentsList_itemDoubleClicked(QListWidgetItem *item)
{
    auto it = std::find_if(attachs.begin(),attachs.end(),[item] (auto & o) { return o.itm == item; });
    if (it != attachs.end()) {
        next_attach = &(*it);
        ui->statusbar->showMessage(it->shrt + " will be attached to your message");
    }
}

void MainWnd::on_actionRefresh_chat_box_triggered()
{
    ui->ChatLog->setMarkdown(cur_chat);
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
    QString fn = qApp->applicationDirPath() + "/" + ANNA_QUICK_FILE;
    std::string dlg = cur_chat.toStdString();
    if (brain->SaveState(fn.toStdString(),dlg.data(),dlg.size()))
        ui->statusbar->showMessage("Quicksave point saved");
    else
        ui->statusbar->showMessage("Error: "+QString::fromStdString(brain->getError()));
}

void MainWnd::on_actionQuick_load_triggered()
{
    QString fn = qApp->applicationDirPath() + "/" + ANNA_QUICK_FILE;
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
