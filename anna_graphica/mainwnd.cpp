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
    ui->UserInput->installEventFilter(this);
    ui->statusbar->installEventFilter(this);
    ui->menubar->installEventFilter(this);
    ui->AttachmentsList->setIconSize(QSize(GUI_ICON_W,GUI_ICON_H));

    on_actionSimple_view_triggered();
    DefaultConfig();
    next_attach = nullptr;
    last_username = false;

    ui->statusbar->showMessage("ANNA version " ANNA_VERSION);
}

MainWnd::~MainWnd()
{
    if (brain) delete brain;
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
    p->n_predict = -1;
    p->n_ctx = 4096;
    p->n_batch = 512;
    p->n_gpu_layers = 0;
    p->model.clear();
    p->prompt.clear();
    p->sampling_params.temp = 0.3;
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
    brain->setInput(config.params.prompt);
    while (brain->Processing(true) == ANNA_PROCESSING) ;
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

void MainWnd::Generate()
{
    bool skips = false;
    std::string str,convo;
    QString old = ui->ChatLog->toMarkdown();
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
            convo += str;
            if (convo.ends_with(ui->UserNameBox->currentText().toStdString())) {
                last_username = true;
                s = ANNA_TURNOVER;
                convo.erase(convo.rfind(ui->UserNameBox->currentText().toStdString()));
                qDebug("convo = %s\n",convo.c_str());
            }
            break;
        case ANNA_ERROR:
            ui->statusbar->showMessage("Error: " + QString::fromStdString(brain->getError()));
            return;
        default:
            break;
        }

        ui->statusbar->showMessage("Brain state: " + QString::fromStdString(AnnaBrain::StateToStr(s)));
        ui->ChatLog->setMarkdown(old + QString::fromStdString(convo));
        ui->ChatLog->moveCursor(QTextCursor::End);
        ui->ChatLog->ensureCursorVisible();
        if (s == ANNA_TURNOVER) break;
        qApp->processEvents();
    }

    ui->ChatLog->setMarkdown(old + QString::fromStdString(convo) + "\n");
    ui->ChatLog->moveCursor(QTextCursor::End);
    ui->ChatLog->ensureCursorVisible();
}


void MainWnd::on_actionSimple_view_triggered()
{
    ui->AINameBox->hide();
    ui->AttachmentsList->hide();
    ui->UserNameBox->hide();
    ui->UserInputOptions->hide();
    ui->statusbar->showMessage("Hint: Hit Enter to submit your text");
}


void MainWnd::on_actionAdvanced_view_triggered()
{
    ui->AINameBox->hide();
    ui->AttachmentsList->show();
    ui->UserNameBox->show();
    ui->UserInputOptions->show();
    ui->SamplingCheck->hide();
    ui->NewlineCheck->hide();
    ui->BeforeRadio->hide();
    ui->AfterRadio->hide();
    ui->statusbar->showMessage("Hint: Use Shift+Enter to submit your text");
}


void MainWnd::on_actionProfessional_view_triggered()
{
    ui->AINameBox->show();
    ui->AttachmentsList->show();
    ui->UserNameBox->show();
    ui->UserInputOptions->show();
    ui->SamplingCheck->show();
    ui->NewlineCheck->show();
    ui->BeforeRadio->show();
    ui->AfterRadio->show();
    ui->statusbar->showMessage("Hint: Use Shift+Enter to submit your text");
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
            brain->setInput(usr.toStdString());
            while (brain->Processing(true) == ANNA_PROCESSING) ;
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
        if (ui->UserInputOptions->isHidden()) eat_ret = true;
        else if (ke->modifiers().testFlag(Qt::ShiftModifier)) eat_ret = true;
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

    brain->setInput(config.params.prompt);
    while (brain->Processing(true) == ANNA_PROCESSING) ;
    ui->statusbar->showMessage("Brain has been reset and is now ready.");

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

