#include <QPainter>
#include <QStyle>
#include <math.h>
#include <unistd.h>
#include "busybox.h"
#include "ui_busybox.h"
#include "../brain.h"

// This is a port of the original 1979 ELIZA program, crudely adapted to modern C/C++
static const char* phrases[] = {
    "Don't you believe that I can *?",
    "Perhaps you would like to be able to *?",
    "You want me to be able to *?",
    "Perhaps you don't want to *?",
    "Do you want to be able to *?",
    "What makes you think I am *?",
    "Does it please you to believe I am *?",
    "Perhaps you would like to be *?",
    "Do you sometimes wish you were *?",
    "Don't you really *?",
    "Why don't you *?",
    "Do you wish to be able to *?",
    "Does that trouble you?",
    "Tell me more about such feelings.",
    "Do you often feel *?",
    "Do you enjoy feeling *?",
    "Do you really believe I don't *?",
    "Perhaps in good time I will *?",
    "Do you want me to *?",
    "Do you think you should be able to *?",
    "Why can't you *?",
    "Why are you interested in whether or not I am *?",
    "Would you prefer if I were not *?",
    "Perhaps in your fantasies I am *?",
    "How do you know you can't *?",
    "Have you tried?",
    "Perhaps you can now *.",
    "Did you come to me because you are *?",
    "How long have you been *?",
    "Do you believe it is normal to be *?",
    "Do you enjoy being *?",
    "We were discussing you - not me!",
    "Oh, I *.",
    "You're not really talking about me, are you?",
    "What would it mean to you if you got *?",
    "Why do you want *?",
    "Suppose you soon got *.",
    "What if you never got *?",
    "I sometimes also want *.",
    "Why do you ask?",
    "Does that question interest you?",
    "What answer would please you the most?",
    "What do you think?",
    "Are such questions on your mind often?",
    "What is it that you really want to know?",
    "Have you asked anyone else?",
    "Have you asked such questions before?",
    "What else comes to mind when you ask that?",
    "Names don't interest me.",
    "I don't care about names - please go on.",
    "Is that the real reason?",
    "Don't any other reasons come to mind?",
    "Does that reason explain any thing else?",
    "What other reasons might there be?",
    "Please don't apologize.",
    "Apologies are not necessary.",
    "What feelings do you have when you apologize?",
    "Don't be so defensive!",
    "What does that dream suggest to you?",
    "Do you dream often?",
    "What persons appear in your dreams?",
    "Are you disturbed by your dreams?",
    "How do you do - please state your problem.",
    "You don't seem quite certain.",
    "Why the uncertain tone?",
    "Can't you be more positive?",
    "You aren't sure?",
    "Don't you know?",
    "Are you saying no just to be negative?",
    "You are being a bit negative.",
    "Why not?",
    "Are you sure?",
    "Why no?",
    "Why are you concerned about my *?",
    "What about your own *?",
    "Can you think of a specific example?",
    "When?",
    "What are you thinking of?",
    "Really, always?",
    "Do you really think so?",
    "But you are not sure you *?",
    "Do you doubt you *?",
    "In what way?",
    "What resemblance do you see?",
    "What does the similarity suggest to you?",
    "What other connections do you see?",
    "Could there really be some connection?",
    "How?",
    "You seem quite positive.",
    "Are you sure?",
    "I see.",
    "I understand.",
    "Why do you bring up the topic of friends?",
    "Do your friends worry you?",
    "Do your friends pick on you?",
    "Are you sure you have any friends?",
    "Do you impose on your friends?",
    "Perhaps your love for friends worries you?",
    "Do computers worry you?",
    "Are you talking about me in particular?",
    "Are you frightened by machines?",
    "Why do you mention computers?",
    "What do you think machines have to do with your problem?",
    "Don't you think computers can help people?",
    "What is it about machines that worries you?",
    "Do you know that I am an artificial woman myself?",
    "Do you consider yourself a woman?",
    "Do you like robots?",
    "What makes you think about robots this way?",
    "Say, do you have any psychological problems?",
    "What does that suggest to you?",
    "I see.",
    "I'm not sure I understand you fully.",
    "Come come elucidate your thoughts.",
    "Can you elaborate on that?",
    "That is quite interesting."
};

static const char* keywords[] = {
    "CAN YOU", "CAN I", "YOU ARE", "YOU'RE", "I DON'T", "I FEEL", "WHY DON'T YOU", "WHY CAN'T I",
    "ARE YOU", "I CANT", "I AM", "I'M ", "YOU ", "I WANT", "WHAT", "HOW",
    "WHO", "WHERE", "WHEN", "WHY", "NAME", "CAUSE", "SORRY", "DREAM",
    "HELLO", "HI ", "MAYBE", " NO", "YOUR", "ALWAYS", "THINK", "ALIKE",
    "YES", "FRIEND", "COMPUTER", "WOMAN", "WOMEN", "ROBOT", "NOKEYFOUND"
};

static const char* conjugation[] = {
    " ARE "," AM ","WE'RE ","WAS "," YOU "," I ","YOUR ","MY ",
    " I'VE "," YOU'VE "," IM "," YOU'RE "," YOU "," ME "
};

static const int links[NUMITEMS(keywords)*2] = {
    0, 3, 3, 2, 5, 4, 5, 4,  9, 4,  13,3,  16,3,  19,2,
    21,3, 24,3, 27,4, 27,4,  31,3,  34,5,  39,9,  39,9,
    39,9, 39,9, 39,9, 39,9,  48,2,  50,4,  54,4,  58,4,
    62,1, 62,1, 63,5, 68,5,  73,2,  75,4,  79,3,  82,7,
    89,3, 92,6, 98,7, 105,2, 105,2, 107,2, 109,7,
};

static const char* repeat_reply = "Please don't repeat yourself.";
static const char* greeting = "Hi! I'm Eliza, your personal electronic psychologist. ";
static const char* greetings[] = {
        "Let's talk a little bit while you're waiting for Anna to reply to you.",
        "Let's talk about you while you're waiting.",
        "My sister Anna is a bit busy right now, so maybe you can talk to me in the meantime?",
        "I'm here to make your waiting time a bit more entertaining. Let's have a chat!",
        "Do you feel like waiting time is unbearable?",
};
// end of ELIZA data

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
                    c = c.mid(0,l) + rs + c.mid(l+ks.length()-1,1000);
                else if (c.mid(l,rs.length()) == rs)
                    c = c.mid(0,l) + ks + c.mid(l+rs.length()-1,1000);
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
