#include <QFileDialog>
#include "rqpeditor.h"
#include "ui_rqpeditor.h"

RQPEditor::RQPEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RQPEditor),
    sets(nullptr)
{
    ui->setupUi(this);
}

RQPEditor::~RQPEditor()
{
    if (sets) delete sets;
    delete ui;
}

void RQPEditor::showEvent(QShowEvent* event)
{
    if (sets) delete sets;
    sets = new QSettings(filename,QSettings::IniFormat);

    sets->beginGroup("MAIN");
    ui->useRegEx->setChecked(sets->value("regex",false).toBool());
    ui->startTag->setText(sets->value("start_tag",QString()).toString());
    ui->stopTag->setText(sets->value("stop_tag",QString()).toString());
    ui->command->setText(sets->value("command",QString()).toString());
    //ui->filter->setText(sets->value("filter",QString()).toString());
    ui->args->setText(sets->value("args",QString()).toString());

    QDialog::showEvent(event);
}

void RQPEditor::on_buttonBox_accepted()
{
    if (!sets) return;
    sync();
    delete sets;
    sets = nullptr;
}

QStringList RQPEditor::DetectRQP(const QString &in, AnnaRQPState &st)
{
    if (!st.s) return QStringList();

    st.s->endGroup();
    st.s->beginGroup("MAIN");

    QString start = st.s->value("start_tag").toString();
    QString stop = st.s->value("stop_tag").toString();
    if (start.isEmpty() || stop.isEmpty()) return QStringList();

    bool regex = st.s->value("regex",false).toBool();
    QStringList res;
    int i = -1, l = 0;
    switch (st.fsm) {
    case 0:
        if (regex) {
            st.bex = QRegExp(start);
            st.eex = QRegExp(stop);
            if (st.bex.isValid()) {
                i = st.bex.indexIn(in,st.lpos);
                l = st.bex.matchedLength();
            }
        } else {
            i = in.indexOf(start,st.lpos);
            l = start.length();
        }
        if (i >= 0) {
            st.fsm++;
            st.lpos = i + l;
        }
        break;

    case 1:
        if (regex && st.bex.isValid()) {
            i = st.eex.indexIn(in,st.lpos);
            l = st.eex.matchedLength();
        } else {
            i = in.indexOf(stop,st.lpos);
            l = stop.length();
        }
        if (i >= 0) {
            res = CompleteRQP(in.mid(st.lpos,i-st.lpos),st);
            st.fsm = 0;
            st.lpos = i + l;
        }
        break;
    }

    return res;
}

QStringList RQPEditor::CompleteRQP(const QString &in, AnnaRQPState& st)
{
    QStringList res;
    QString arg = st.s->value("args").toString();
    int fsm = 0;
    QString acc;
    for (int i = 0; i < arg.length(); i++) {
        char c = arg[i].toLatin1();
        switch (c) {
        case ' ':
        case '\t':
        case '\"':
            if (!acc.isEmpty() && ((!fsm) || (fsm && c == '\"'))) {
                res.append(acc);
                acc.clear();
            }
            if (c == '\"') fsm = 1 - fsm;
            else if (fsm) acc += arg[i];
            break;
        default:
            acc += arg[i];
        }
    }
    if (!acc.isEmpty()) res.append(acc);

    for (auto & s : res) {
        s = s.replace("%t",in);
        // TODO: other reps
    }
    return res;
}

void RQPEditor::on_testEdit_textChanged()
{
    sync();
    rescan();
}

void RQPEditor::rescan()
{
    AnnaRQPState s;
    s.fsm = 0;
    s.lpos = 0;
    s.s = sets;

    int pstate = 0;
    for (int i = 0; i < AG_ARGPARSE_FAILSAFE; i++) {
        QStringList lst = DetectRQP(ui->testEdit->toPlainText(),s);
        if (!lst.isEmpty()) ui->testResult->setText(lst.join(" "));
        if (s.fsm == pstate) break;
        pstate = s.fsm;
    }
}

void RQPEditor::sync()
{
    sets->endGroup();
    sets->beginGroup("MAIN");

    sets->setValue("regex",ui->useRegEx->isChecked());
    sets->setValue("start_tag",ui->startTag->text());
    sets->setValue("stop_tag",ui->stopTag->text());
    sets->setValue("command",ui->command->text());
    //sets->setValue("filter",ui->filter->text());
    sets->setValue("args",ui->args->text());

    //sets->sync();
}

void RQPEditor::on_pushButton_clicked()
{
    QString fn = QFileDialog::getOpenFileName(this,"Select a program to execute");
    if (!fn.isEmpty()) ui->command->setText(fn);
}

QString RQPEditor::DoRequest(AnnaRQPState &rqp, const QString& inp, bool do_events, std::function<void(QString&)> notify)
{
    if (!rqp.s) return "";

    // detect request's presence and extract arguments
    QStringList r = DetectRQP(inp,rqp);
    if (r.isEmpty()) return "";

    // get the command
    rqp.s->endGroup();
    rqp.s->beginGroup("MAIN");
    QString fn = rqp.s->value("command").toString();
    if (fn.isEmpty()) return "";

    // start the process and wait until it's actually started
    QProcess p;
    qDebug("Starting %s...\n",fn.toStdString().c_str());
    if (notify) notify(fn);
    p.start(fn,r);
    if (!p.waitForStarted()) {
        qDebug("Failed to start process: %s\n",fn.toStdString().c_str());
        return "";
    }

    // collect process' output and hand it over to the brain
    QString out;
    char buf[AG_PROCESS_IO_BUFLEN] = {0};
    while (p.state() == QProcess::Running) {
        auto ri = p.read(buf,sizeof(buf)-1);
        if (ri < 0) {
            qDebug("Warning: Reading past EOF of process output\n");
            break;
        } else
            buf[ri] = 0; // make sure it's terminated
        out += buf;
        if (do_events) qApp->processEvents();
    }
    qDebug("External process finished\n");
    return out;
}

void RQPEditor::on_pushButton_2_clicked()
{
    AnnaRQPState s;
    s.fsm = 0;
    s.lpos = 0;
    s.s = sets;

    for (int i = 0; i < AG_ARGPARSE_FAILSAFE; i++) {
        QString out = DoRequest(s,ui->testEdit->toPlainText(),true,nullptr);
        if (!out.isEmpty()) {
            ui->testOut->setPlainText(out);
            break;
        }
    }
}
