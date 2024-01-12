#include "mainwnd.h"
#include "ui_mainwnd.h"

MainWnd::MainWnd(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWnd)
    , brain(nullptr)
{
    ui->setupUi(this);
    ui->UserInput->installEventFilter(this);
    on_actionSimple_view_triggered();
    ui->statusbar->showMessage("ANNA version " ANNA_VERSION);
}

MainWnd::~MainWnd()
{
    if (brain) delete brain;
    delete ui;
}

void MainWnd::LoadLLM(const QString &fn)
{
    if (brain) {
        // delete old brain first
        // TODO: add confirmation dialog if there's an unsaved state/conversation
        delete brain;
        brain = nullptr;
    }

    // Fill in the config
    // TODO: add user-definable settings
    AnnaConfig c;
    c.convert_eos_to_nl = true;
    c.verbose_level = 1; // FIXME: ???

    gpt_params* p = &c.params;
    p->model = fn.toStdString();
    p->prompt = "SYSTEM: You're a helpful AI assistant named Anna. You're helping your user with their daily tasks.\n"; // FIXME: load it from external file
    p->seed = 0;
    p->n_threads = 8;
    p->n_predict = -1;
    p->n_ctx = 4096;
    //p->rope_freq_scale = 0.5;
    p->n_batch = 512;
    p->n_gpu_layers = 0; //43;

    ui->statusbar->showMessage("Loading LLM file... Please wait!");
    qApp->processEvents();

    brain = new AnnaBrain(&c);
    if (brain->getState() != ANNA_READY) {
        ui->statusbar->showMessage("Unable to load LLM file!");
        delete brain;
        brain = nullptr;
        return;
    }
    ui->statusbar->showMessage("LLM file loaded. Please wait for system prompt processing...");
    qApp->processEvents();

    brain->setInput(p->prompt);
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
    if (event->type() == QEvent::KeyPress) {
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
    } else {
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
}


void MainWnd::on_AttachButton_clicked()
{
    // FIXME: temporary. debug-only!
    //ui->statusbar->showMessage(QString::fromStdString(AnnaBrain::state_to_string(brain->Processing())));
    //ui->ChatLog->append(QString::fromStdString(brain->getOutput()));
}


void MainWnd::on_AINameBox_currentIndexChanged(const QString &arg1)
{
    //ForceAIName(arg1);
}


void MainWnd::on_actionLoad_model_triggered()
{
    on_ModelFindButton_clicked();
}

