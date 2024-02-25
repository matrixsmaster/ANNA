#ifndef BUSYBOX_H
#define BUSYBOX_H

#include <QDialog>

#define GUI_BUSYBX_ELRAD 20
#define GUI_BUSYBX_SPD 6.5
#define GUI_BUSYBX_STP 10.f
#define GUI_BUSYBX_MARGIN 0.87

namespace Ui {
class BusyBox;
}

class BusyBox : public QDialog
{
    Q_OBJECT

public:
    explicit BusyBox(QWidget *parent, QRect base);
    ~BusyBox();

    void Use(int progress);

protected:
    virtual void paintEvent(QPaintEvent *event) override;

private:
    Ui::BusyBox *ui;
    float angle,cx,cy;

    void draw();
    void pos(float a);
};

#endif // BUSYBOX_H
