#include "mainwnd.h"
#include "ui_mainwnd.h"

MainWnd::MainWnd(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWnd)
    , brain(nullptr)
{
    ui->setupUi(this);
    ui->UserInput->installEventFilter(this);
    ui->statusbar->installEventFilter(this);
    ui->menubar->installEventFilter(this);
    on_actionSimple_view_triggered();
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
    config.verbose_level = 1;

    gpt_params* p = &config.params;
    p->seed = 0;
    p->n_threads = 8;
    p->n_predict = -1;
    p->n_ctx = 4096;
    //p->rope_freq_scale = 0.5;
    p->n_batch = 512;
    p->n_gpu_layers = 0; //43;
    p->model.clear();
    p->prompt.clear();
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
    //ForceAIName(ui->AINameBox->currentText());

    while (brain) {
        AnnaState s = brain->Processing(skips);
        switch (s) {
        case ANNA_READY:
        case ANNA_TURNOVER:
            str = brain->getOutput();
            qDebug("str = %s\n",str.c_str());
            convo += str;
            if (convo.ends_with(ui->UserNameBox->currentText().toStdString())) {
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

        ui->statusbar->showMessage("Brain state: " + QString::fromStdString(AnnaBrain::state_to_string(s)));
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
    QString fn = QFileDialog::getOpenFileName(this,"Open LLM file","","GGUF files (*.gguf)");
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

    QString usr, line, log = ui->ChatLog->toMarkdown();
    if (!ui->ChatLog->toPlainText().endsWith("\n")) {
        usr = "\n";
        if (!ui->NewlineCheck->isChecked()) log += "\n**";
        else log += "\n";
    }

    line = ui->UserNameBox->currentText() + " " + ui->UserInput->toPlainText();
    usr += line;
    log += line;

    if (!ui->NewlineCheck->isChecked()) {
        // normal behavior
        usr += "\n";
        log += "**\n";
        ForceAIName(ui->AINameBox->currentText());
    } else
        ForceAIName(""); // no newline, no enforcing

    brain->setInput(usr.toStdString());
    ui->UserInput->clear();
    ui->ChatLog->setMarkdown(log);

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
    // FIXME: temporary. debug-only!
    //ui->statusbar->showMessage(QString::fromStdString(AnnaBrain::state_to_string(brain->Processing())));
    //ui->ChatLog->append(QString::fromStdString(brain->getOutput()));
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
}


void MainWnd::on_actionQuit_triggered()
{
    close();
}


void MainWnd::on_actionMarkdown_triggered()
{
    QString fn = QFileDialog::getOpenFileName(this,"Save dialog","","Markdown files (*.md);;Text files (*.txt)");
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

