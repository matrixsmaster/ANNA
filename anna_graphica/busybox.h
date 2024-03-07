#ifndef BUSYBOX_H
#define BUSYBOX_H

#include <QDialog>
//#include <mutex>

#define NUMITEMS(A) ((int)(sizeof(A) / sizeof((A)[0])))

#define GUI_BUSYBX_ELRAD 20
#define GUI_BUSYBX_SPD 6.5
#define GUI_BUSYBX_STP 10.f
#define GUI_BUSYBX_MARGIN 0.92

namespace Ui {
class BusyBox;
}

class BusyBox : public QDialog
{
    Q_OBJECT

public:
    explicit BusyBox(QWidget *parent);
    ~BusyBox();

    void Use(QRect base, int progress);

protected:
    virtual void paintEvent(QPaintEvent *event) override;

private slots:
    void on_usrInput_returnPressed();

    void on_pushButton_clicked();

private:
    Ui::BusyBox *ui;
    float angle,cx,cy;
    QString prev, repbuf;
    std::vector<int> rep_start, rep_stop, rep_cur;

    void draw();
    void pos(float a);
    void test_ai();
    QString think(QString in);
    void print();
};

#endif // BUSYBOX_H
