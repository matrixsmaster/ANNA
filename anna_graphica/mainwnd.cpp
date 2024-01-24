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

    on_actionSimple_view_triggered();
    DefaultConfig();
    LoadSettings();
    next_attach = nullptr;
    last_username = false;

    ui->statusbar->showMessage("ANNA version " ANNA_VERSION);
}

MainWnd::~MainWnd()
{
    SaveSettings();
    if (brain) delete brain;
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
    p->model.clear();
    p->prompt.clear();
    p->sampling_params.temp = ANNA_DEFAULT_TEMP;

    config.user = &guiconfig;
    guiconfig.enter_key = 0;
    guiconfig.md_fix = false;
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

void MainWnd::LoadLLM(const QString &fn)
{
    // Create new Brain
    if (brain) {
        // delete old brain first
        // TODO: add confirmation dialog if there's an unsaved state/conversation
        delete brain;
        brain = nullptr;
    }

    // Fill in the config
    config.params.model = fn.toStdString();
    if (config.params.prompt.empty()) {
        auto uq = QMessageBox::question(this,"ANNA","Do you want to open a prompt file?\nIf answered No, a default prompt will be used.",
                                        QMessageBox::No | QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::No);
        config.params.prompt = ANNA_DEFAULT_PROMPT;
        if (uq == QMessageBox::Yes)
            on_actionLoad_initial_prompt_triggered();
        else if (uq == QMessageBox::Cancel)
            return;
    }

    // Actual loading
    ui->statusbar->showMessage("Loading LLM file... Please wait!");
    qApp->processEvents();
    brain = new AnnaBrain(&config);
    if (brain->getState() != ANNA_READY) {
        ui->statusbar->showMessage("Unable to load LLM file!");
        delete brain;
        brain = nullptr;
        return;
    }
    ui->statusbar->showMessage("LLM file loaded. Please wait for system prompt processing...");
    qApp->processEvents();

    // Process initial prompt
    ProcessInput(config.params.prompt);
    seed_label->setText(QString("Seed: %1").arg((int)brain->getConfig()->params.seed));
    ui->statusbar->showMessage("Brain is ready");
}

void MainWnd::ForceAIName(const QString &nm)
{
    if (!brain) return;

    if (nm.isEmpty() || nm == "<none>") {
        brain->setPrefix(std::string());
        qDebug("AI prefix removed");
        return;
    }

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
        qApp->processEvents();
    }
}

void MainWnd::Generate()
{
    bool skips = false;
    std::string str;
    QString convo;
    stop = false;

    while (brain && !stop) {
        AnnaState s = brain->Processing(skips);
        switch (s) {
        case ANNA_READY:
            if (ui->SamplingCheck->isChecked()) return;
            // fall-thru
        case ANNA_TURNOVER:
            str = brain->getOutput();
            qDebug("str = %s\n",str.c_str());
            convo += QString::fromStdString(str);
            if (convo.endsWith(ui->UserNameBox->currentText())) {
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
        if (s == ANNA_TURNOVER) break;
        qApp->processEvents();
    }

    cur_chat += convo + "\n";
    ui->ChatLog->setMarkdown(cur_chat);
    ui->ChatLog->moveCursor(QTextCursor::End);
    ui->ChatLog->ensureCursorVisible();
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
    QString fn = GetOpenFileName(ANNA_FILE_LLM);
    if (fn.isEmpty()) return;
    ui->ModelPath->setText(fn);
    ui->ChatLog->clear();
    cur_chat.clear();
    LoadLLM(fn);
}

void MainWnd::on_SendButton_clicked()
{
    if (!brain) return;

    QString usr, line, log, infixed;
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

    infixed = ui->UserInput->toPlainText();
    if (guiconfig.md_fix) FixMarkdown(infixed);
    usr += line + ui->UserInput->toPlainText();
    log += line + infixed;

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

    qDebug("usr = '%s'\n",usr.toStdString().c_str());
    qDebug("log = '%s'\n",log.toStdString().c_str());

    cur_chat += log;
    ui->ChatLog->setMarkdown(cur_chat);
    ui->ChatLog->moveCursor(QTextCursor::End);
    ui->ChatLog->ensureCursorVisible();
    ui->statusbar->showMessage("Processing...");
    qApp->processEvents();

    brain->setInput(usr.toStdString());
    Generate();
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
    if (!cur_chat.isEmpty()) {
        auto b = QMessageBox::question(this,"ANNA","Reset dialog?\n",QMessageBox::No | QMessageBox::Yes,QMessageBox::Yes);
        if (b != QMessageBox::Yes) return;
    }

    if (brain) {
        brain->Reset();
        ui->statusbar->showMessage("Brain reset complete. Please wait for prompt processing...");
        qApp->processEvents();

        ProcessInput(config.params.prompt);
        ui->statusbar->showMessage("Brain has been reset and is now ready.");
    }

    cur_chat.clear();
    ui->UserInput->clear();
    ui->ChatLog->clear();
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
    config.params.prompt = np.toStdString();
}

void MainWnd::on_actionSettings_triggered()
{
    SettingsDialog sdlg;
    sdlg.pconfig = &config;
    if (sdlg.exec() == QDialog::Accepted && brain) {
        // update things which can be updated on the fly
        brain->getConfig()->convert_eos_to_nl = config.convert_eos_to_nl;
        brain->getConfig()->nl_to_turnover = config.nl_to_turnover;
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
    if (brain->SaveState(fn.toStdString()))
        ui->statusbar->showMessage("Model state has been saved to "+fn);
    else
        ui->statusbar->showMessage("Unable to save model state!");
}

void MainWnd::on_actionLoad_state_triggered()
{
    if (!brain) return;
    QString fn = GetOpenFileName(ANNA_FILE_MODEL_STATE);
    if (fn.isEmpty()) return;
    if (brain->LoadState(fn.toStdString()))
        ui->statusbar->showMessage("Model state has been loaded from "+fn);
    else
        ui->statusbar->showMessage("Unable to load model state!");
}

void MainWnd::on_actionShow_prompt_triggered()
{
    QString r = QString::fromStdString(config.params.prompt);
    if (guiconfig.md_fix) FixMarkdown(r);
    cur_chat = r + "\n\n" + cur_chat;
    ui->ChatLog->setMarkdown(cur_chat);
    ui->ChatLog->moveCursor(QTextCursor::End);
    ui->ChatLog->ensureCursorVisible();
}

void MainWnd::FixMarkdown(QString& s)
{
    int fsm = 0;
    //TODO: catch situations where bold flag was interrupted by newline
    for (int i = 0; i < s.length(); i++) {
        char n = 0;
        switch (fsm) {
        case 0:
            if (s[i].isPrint()) {
                switch (s[i].toLatin1()) {
                case '#': n = '\\'; break;
                default: fsm++;
                }
            }
            break;
        case 1:
            if (s[i] == '\n') fsm++;
            break;
        case 2:
            if (s[i] != '\n') n = '\n';
            fsm = 0;
            break;
        }
        if (n) {
            s.insert(i,n);
            i++;
        }
    }
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
}

void MainWnd::on_actionShow_context_tokens_triggered()
{
    if (brain) ui->ChatLog->setPlainText(QString::fromStdString(brain->PrintContext()));
}

void MainWnd::on_actionQuick_save_triggered()
{
    if (!brain) return;
    QString fn = qApp->applicationDirPath() + "/" + ANNA_QUICK_FILE;
    if (brain->SaveState(fn.toStdString()))
        ui->statusbar->showMessage("Model state has been saved to "+fn);
    else
        ui->statusbar->showMessage("Unable to save model state!");
}

void MainWnd::on_actionQuick_load_triggered()
{
    if (!brain) return;
    QString fn = qApp->applicationDirPath() + "/" + ANNA_QUICK_FILE;
    if (brain->LoadState(fn.toStdString()))
        ui->statusbar->showMessage("Model state has been loaded from "+fn);
    else
        ui->statusbar->showMessage("Unable to load model state!");
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