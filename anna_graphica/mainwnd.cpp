#include "mainwnd.h"
#include "ui_mainwnd.h"
#include "settingsdialog.h"
#include "aboutbox.h"

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

    SettingsDialog::LoadSettings(&config,&s);
}

void MainWnd::SaveSettings()
{
    QString ini = qApp->applicationDirPath() + "/" + ANNA_CONFIG_FILE;
    QSettings s(ini,QSettings::IniFormat);
    SettingsDialog::SaveSettings(&config,&s);
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
    seed_label->setText(QString("Seed: %1").arg((int)brain->getSeed()));
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
    QString convo,old = ui->ChatLog->toMarkdown();
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
        ui->ChatLog->setMarkdown(old + convo);
        ui->ChatLog->moveCursor(QTextCursor::End);
        ui->ChatLog->ensureCursorVisible();
        ui->ContextFull->setMaximum(config.params.n_ctx);
        ui->ContextFull->setValue(brain->getTokensUsed());
        if (s == ANNA_TURNOVER) break;
        qApp->processEvents();
    }

    ui->ChatLog->setMarkdown(old + convo + "\n");
    ui->ChatLog->moveCursor(QTextCursor::End);
    ui->ChatLog->ensureCursorVisible();
}


void MainWnd::on_actionSimple_view_triggered()
{
    ui->AINameBox->hide();
    ui->AttachmentsList->hide();
    ui->ContextFull->hide();
    ui->UserNameBox->hide();
    ui->UserInputOptions->hide();
    seed_label->hide();
}


void MainWnd::on_actionAdvanced_view_triggered()
{
    ui->AINameBox->show();
    ui->AttachmentsList->show();
    ui->ContextFull->show();
    ui->UserNameBox->show();
    ui->UserInputOptions->show();
    ui->SamplingCheck->hide();
    ui->NewlineCheck->hide();
    ui->BeforeRadio->hide();
    ui->AfterRadio->hide();
    seed_label->show();
}


void MainWnd::on_actionProfessional_view_triggered()
{
    ui->AINameBox->show();
    ui->AttachmentsList->show();
    ui->ContextFull->show();
    ui->UserNameBox->show();
    ui->UserInputOptions->show();
    ui->SamplingCheck->show();
    ui->NewlineCheck->show();
    ui->BeforeRadio->show();
    ui->AfterRadio->show();
    seed_label->show();
}


void MainWnd::on_ModelFindButton_clicked()
{
    QString fn = QFileDialog::getOpenFileName(this,"Open LLM file",ui->ModelPath->text(),"GGUF files (*.gguf)");
    if (fn.isEmpty()) return;
    ui->ModelPath->setText(fn);
    LoadLLM(fn);
}


void MainWnd::on_SendButton_clicked()
{
    if (!brain) return;
    if (ui->UserInput->toPlainText().isEmpty()) {
        Generate();
        return;
    }

    QString usr, line, log;
    // TODO: fix visible/invisible newlines merging - for now it's quite inaccurate!
    if (!ui->ChatLog->toMarkdown().endsWith("\n")) {
        usr = "\n";
        log = "\n";
    }
    log += "\n**";

    if (last_username) {
        usr.clear(); // no need for initial newline
        log += ui->UserNameBox->currentText(); // we still need to put it into the log
        last_username = false;
    } else
        line = ui->UserNameBox->currentText();

    line += " " + ui->UserInput->toPlainText();
    usr += line;
    log += line;

    if (!ui->NewlineCheck->isChecked()) {
        // normal behavior
        usr += "\n";
        log += "**\n";
        ForceAIName(ui->AINameBox->currentText());
    } else {
        log += "** ";
        ForceAIName(""); // no newline, no enforcing
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

    ui->ChatLog->setMarkdown(ui->ChatLog->toMarkdown() + log);
    ui->ChatLog->moveCursor(QTextCursor::End);
    ui->ChatLog->ensureCursorVisible();
    ui->statusbar->showMessage("Processing...");
    qApp->processEvents();

    brain->setInput(usr.toStdString());
    Generate();
}


void MainWnd::closeEvent(QCloseEvent* event)
{
    auto b = QMessageBox::question(this,"ANNA","Are you sure?\n",QMessageBox::No | QMessageBox::Yes,QMessageBox::Yes);
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
    a.fn = QFileDialog::getOpenFileName(this,"Open file","","Text files (*.txt);;Image files (*.png *.jpg *.jpeg *.bmp *.xpm *.ppm *.pbm *.pgm *.xbm *.xpm)");
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
    if (!brain) return;
    brain->Reset();
    ui->statusbar->showMessage("Brain reset complete. Please wait for prompt processing...");
    qApp->processEvents();

    ProcessInput(config.params.prompt);
    ui->statusbar->showMessage("Brain has been reset and is now ready.");

    ui->UserInput->clear();
    ui->ChatLog->clear();
    last_username = false;
}


void MainWnd::on_actionQuit_triggered()
{
    close();
}


void MainWnd::on_actionMarkdown_triggered()
{
    QString fn = QFileDialog::getSaveFileName(this,"Save dialog","","Markdown files (*.md);;Text files (*.txt)");
    if (fn.isEmpty()) return;
    if (SaveFile(fn,ui->ChatLog->toMarkdown()))
        ui->statusbar->showMessage("Chat log saved as markdown document.");
    else
        ui->statusbar->showMessage("Unable to write output file.");
}


void MainWnd::on_actionLoad_initial_prompt_triggered()
{
    QString fn = QFileDialog::getOpenFileName(this,"Open prompt file","","Text files (*.txt)");
    QString np;
    if (fn.isEmpty() || !LoadFile(fn,np)) return;
    config.params.prompt = np.toStdString();
}


void MainWnd::on_actionSettings_triggered()
{
    SettingsDialog sdlg;
    sdlg.pconfig = &config;
    sdlg.exec();
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

    QString fn = QFileDialog::getOpenFileName(this,"Open CLiP encoder file","","GGUF files (*.gguf)");
    if (fn.isEmpty()) return;
    brain->setClipModelFile(fn.toStdString());
    ui->statusbar->showMessage("CLiP model file set to "+fn);
}


void MainWnd::on_actionPlain_text_triggered()
{
    QString fn = QFileDialog::getSaveFileName(this,"Save dialog","","Text files (*.txt)");
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
}


void MainWnd::on_actionHTML_triggered()
{
    QString fn = QFileDialog::getSaveFileName(this,"Save dialog","","HTML files (*.html *.htm)");
    if (fn.isEmpty()) return;
    if (SaveFile(fn,ui->ChatLog->toHtml()))
        ui->statusbar->showMessage("Chat log saved as HTML document.");
    else
        ui->statusbar->showMessage("Unable to write output file.");
}


void MainWnd::on_actionSave_state_triggered()
{
    if (!brain) return;
    QString fn = QFileDialog::getSaveFileName(this,"Save model state","","ANNA save states (*.anna)");
    if (fn.isEmpty()) return;
    if (brain->SaveState(fn.toStdString()))
        ui->statusbar->showMessage("Model state has been saved to "+fn);
    else
        ui->statusbar->showMessage("Unable to save model state!");
}


void MainWnd::on_actionLoad_state_triggered()
{
    if (!brain) return;
    QString fn = QFileDialog::getOpenFileName(this,"Load model state","","ANNA save states (*.anna)");
    if (fn.isEmpty()) return;
    if (brain->LoadState(fn.toStdString()))
        ui->statusbar->showMessage("Model state has been loaded from "+fn);
    else
        ui->statusbar->showMessage("Unable to load model state!");
}


void MainWnd::on_actionShow_prompt_triggered()
{
    QString r = QString::fromStdString(config.params.prompt) + "\n\n" + ui->ChatLog->toMarkdown();
    ui->ChatLog->setMarkdown(r);
    ui->ChatLog->moveCursor(QTextCursor::End);
    ui->ChatLog->ensureCursorVisible();
}

