#include "mainwnd.h"
#include "ui_mainwnd.h"

MainWnd::MainWnd(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWnd)
    , brain(nullptr)
{
    ui->setupUi(this);
    //on_actionSimple_view_triggered();
    on_actionAdvanced_view_triggered(); // FIXME: debug only, afterwards uncomment previous line
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

    brain->setInput(p->prompt);
    while (brain->Processing(true) == ANNA_PROCESSING) ;
}

#if 0

string convo;
bool skips = false;

while (1) {
    AnnaState s = anna.Processing(skips);
    switch (s) {
    case ANNA_READY:
    case ANNA_TURNOVER:
        {
            string out = anna.getOutput();
            convo += out;
            printf("%s",out.c_str());
            fflush(stdout);
            if (convo.ends_with("User:")) s = ANNA_TURNOVER;
        }
        break;
    case ANNA_ERROR:
        ERR("%s",anna.getError().c_str());
        abort();
        break;
    }

    if (s == ANNA_TURNOVER) {
        anna.setInput(get_input(&skips));
        print_vec(&anna,anna.queue);
        print_vec(&anna,anna.inp_emb);
        print_vec(&anna,anna.ctx_sp->prev);
    }
}

puts(convo.c_str());
return 0;
#endif

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
    /*ui->UserInput->
    brain->setInput("USER: "+)*/
    brain->Processing();
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
