#include "mainwnd.h"
#include "ui_mainwnd.h"

MainWnd::MainWnd(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWnd)
    , brain(nullptr)
{
    ui->setupUi(this);
    //on_actionSimple_view_triggered();
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
    p->n_threads = 10;
    p->n_predict = -1;
    p->n_ctx = 4096;
    //p->rope_freq_scale = 0.5;
    p->n_batch = 512;
    p->n_gpu_layers = 0; //43;

    brain = new AnnaBrain(&c);
    if (brain->getState() != ANNA_READY) {
        ui->statusbar->showMessage("Unable to load LLM file!");
        delete brain;
        brain = nullptr;
        return;
    }
    ui->statusbar->showMessage("LLM loaded");

    //dialog.clear();
    brain->setInput(p->prompt);
    while (brain->Processing(true) == ANNA_PROCESSING) ;
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
            break;
        default:
            break;
        }

        ui->statusbar->showMessage(QString::fromStdString(AnnaBrain::state_to_string(s)));
        ui->ChatLog->setMarkdown(old + QString::fromStdString(convo));
        if (s == ANNA_TURNOVER) break;
        qApp->processEvents();
    }

    ui->ChatLog->setMarkdown(old + QString::fromStdString(convo) + "\n");
}


void MainWnd::on_actionSimple_view_triggered()
{
    ui->AINameBox->hide();
    ui->AttachmentsList->hide();
    ui->UserNameBox->hide();
    ui->UserInputOptions->hide();
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

    QString usr = ui->UserNameBox->currentText() + " " + ui->UserInput->toPlainText();
    ui->ChatLog->setMarkdown(ui->ChatLog->toMarkdown() + usr + "\n");
    brain->setInput(usr.toStdString());
    ui->UserInput->clear();

    Generate();
}


bool TextBoxKeyFilter::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        qDebug("Ate key press %d", keyEvent->key());
        return true;
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
    ForceAIName(arg1);
}


void MainWnd::on_actionLoad_model_triggered()
{
    on_ModelFindButton_clicked();
}

