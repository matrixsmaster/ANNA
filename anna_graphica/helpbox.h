#ifndef HELPBOX_H
#define HELPBOX_H

#include <QDialog>

namespace Ui {
class HelpBox;
}

class HelpBox : public QDialog
{
    Q_OBJECT

public:
    explicit HelpBox(QWidget *parent = nullptr);
    ~HelpBox();

private slots:
    void on_pushButton_4_clicked();

private:
    Ui::HelpBox *ui;
};

#endif // HELPBOX_H
