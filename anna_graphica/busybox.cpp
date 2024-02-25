#include <QPainter>
#include <QStyle>
#include <math.h>
#include <unistd.h>
#include "busybox.h"
#include "ui_busybox.h"

BusyBox::BusyBox(QWidget *parent, QRect base) :
    QDialog(parent),
    ui(new Ui::BusyBox)
{
    ui->setupUi(this);
    setWindowFlags(Qt::FramelessWindowHint);
    setGeometry(QStyle::alignedRect(Qt::LeftToRight,Qt::AlignCenter,size(),base));
    angle = 0;
}

BusyBox::~BusyBox()
{
    delete ui;
}

void BusyBox::Use(int progress)
{
    ui->progressBar->setVisible(progress > 0);
    ui->progressBar->setValue(progress);
    update();
}

void BusyBox::draw()
{
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
    draw();
    QDialog::paintEvent(event);
    //usleep(50000);
    update();
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
