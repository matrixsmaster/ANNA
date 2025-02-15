#include <QPainter>
#include <QStyle>
#include <math.h>
#include <unistd.h>
#include "busybox.h"
#include "ui_busybox.h"
#include "../brain.h"
#include "eliza_data.h"

using namespace std;

BusyBox::BusyBox(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BusyBox)
{
    ui->setupUi(this);
    setWindowFlags(Qt::FramelessWindowHint);
    ui->aiOutput->hide();
    ui->usrInput->hide();

    angle = 0;
    rep_start.resize(NUMITEMS(keywords));
    rep_cur.resize(NUMITEMS(keywords));
    rep_stop.resize(NUMITEMS(keywords));
    for (int x = 0, l = 0; x < NUMITEMS(keywords); x++,l+=2) {
        rep_start[x] = links[l];
        rep_cur[x] = rep_start[x];
        rep_stop[x] = rep_start[x] + links[l+1] - 1;
    }

    close();
}

BusyBox::~BusyBox()
{
    delete ui;
}

bool BusyBox::Use(QRect base, int progress, bool abortable)
{
    if (isHidden()) {
        show();
        if (!ui->usrInput->text().isEmpty())
            ui->usrInput->setCursorPosition(ui->usrInput->text().length());
    }

    setGeometry(QStyle::alignedRect(Qt::LeftToRight,Qt::AlignCenter,size(),base));

    ui->progressBar->setVisible(progress > 0);
    ui->progressBar->setValue(progress);

    if (abortable) ui->cancelButton->show();
    else ui->cancelButton->hide();

    update();
    return abortable? (!aborted) : true;
}

void BusyBox::Reset()
{
    aborted = false;
    ui->cancelButton->hide();
}

void BusyBox::draw()
{
    // draw something for the user to look at while they're waiting
    QPainter painter(this);

    angle += GUI_BUSYBX_SPD;
    if (angle >= 360) angle -= 360;

    for (float a = 0; a < 360.f; a += GUI_BUSYBX_STP) {
        pos(a + angle);
        int r = (a < 1) ? GUI_BUSYBX_ELRAD : GUI_BUSYBX_ELRAD / 2;

        QRadialGradient gr(cx,cy,r/2);
        gr.setColorAt(0,((a < 1)? Qt::magenta : Qt::blue));
        gr.setColorAt(1,palette().color(QPalette::Window));
        QBrush br(gr);
        painter.setBrush(br);
        QPen pn(Qt::NoPen);
        painter.setPen(pn);

        painter.drawEllipse(floor(cx)-r,floor(cy)-r,r*2,r*2);
    }
}

void BusyBox::paintEvent(QPaintEvent *event)
{
    print();
    draw();
    update();
    //usleep(50000);

    QDialog::paintEvent(event);
}

void BusyBox::pos(float a)
{
    a *= M_PI / 180.f;
    float hw = (float)width() / 2.f;
    float hh = (float)height() / 2.f;
    float rm = std::min(hw,hh) * GUI_BUSYBX_MARGIN;
    cx = cos(a) * rm + hw;
    cy = sin(a) * rm + hh;
}

void BusyBox::test_ai()
{
    vector<int> test(NUMITEMS(phrases),0);

    for (int i = 0; i < NUMITEMS(keywords); i++) {
        const char* str = keywords[i];
        for (int j = rep_start[i]; j <= rep_stop[i]; j++) {
            const char* rep = (j >= 0 && j < NUMITEMS(phrases))? phrases[j] : "NOT FOUND";
            qDebug("test_ai(): %s -> %s",str,rep);
            test[j]++; // increment usage number
        }
    }

    qDebug("\ntest_ai(): Reply: Number_of_uses\n");
    for (int i = 0; i < (int)test.size(); i++) {
        string str = AnnaBrain::myformat("%d: %d",i,test[i]);
        if (test[i] < 1) str += "  <-- ALERT !!";
        qDebug("test_ai(): %s",str.c_str());
    }
}

QString BusyBox::think(QString in)
{
    if (in.isEmpty()) return "";

    in.replace(QRegExp("[!.?*]")," ");
    in = " " + in.toUpper() + " ";
    if (in == prev) return repeat_reply;
    prev = in;

    int si = -1, l = -1, x = -1, k = -1;
    QString f, c;

    for (int k = 0; k < NUMITEMS(keywords); k++) {
        l = in.lastIndexOf(keywords[k]);
        if (l >= 0) {
            si = k;
            x = l;
            f = keywords[k];
            break;
        }
    }

    if (si >= 0) {
        k = si;
        l = x;

        while ((in.length() - f.length() - l) < 0) --l;

        c = " " + in.mid(f.length()+l,in.length()-f.length()+l);

        si = 0;
        for (x = 0; x < NUMITEMS(conjugation) / 2; x++) {
            QString ks = conjugation[si];
            QString rs = conjugation[si+1];
            si += 2;
            for (l = 0; l < c.length(); l++) {
                if (c.mid(l,ks.length()) == ks)
                    c = c.mid(0,l) + rs + c.mid(l+ks.length()-1);
                else if (c.mid(l,rs.length()) == rs)
                    c = c.mid(0,l) + ks + c.mid(l+rs.length()-1);
            }
        }
        if (c.mid(0,1) == " ") c.remove(0,1);

    } else
        k = NUMITEMS(keywords) - 1;

    f = phrases[rep_cur[k]];
    rep_cur[k] = rep_cur[k] + 1;
    if (rep_cur[k] > rep_stop[k]) rep_cur[k] = rep_start[k];

    return f.replace("*",c.toLower());
}

void BusyBox::print()
{
    if (repbuf.isEmpty() || ui->aiOutput->isHidden()) return;

    int n = rand() & 4;
    ui->aiOutput->setPlainText(ui->aiOutput->toPlainText() + repbuf.mid(0,n));
    ui->aiOutput->moveCursor(QTextCursor::End);
    ui->aiOutput->ensureCursorVisible();
    repbuf.remove(0,n);
}

void BusyBox::on_usrInput_returnPressed()
{
    repbuf = think(ui->usrInput->text());
    if (repbuf.isEmpty()) return;
    repbuf += "\n";

    ui->aiOutput->setPlainText(ui->aiOutput->toPlainText() + "You: " + ui->usrInput->text() + "\nEliza: ");
    ui->usrInput->clear();
}

void BusyBox::on_pushButton_clicked()
{
    repbuf = greeting;
    repbuf += greetings[rand() % NUMITEMS(greetings)];
    repbuf += "\n";

    ui->aiOutput->show();
    ui->usrInput->show();
    ui->pushButton->hide();
}

void BusyBox::on_cancelButton_clicked()
{
    aborted = true;
}
