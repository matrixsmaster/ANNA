#ifndef ABOUTBOX_H
#define ABOUTBOX_H

#include <QDialog>

#define COPYRIGHT   "Copyright (c) Georgi Gerganov, 2023\n" \
                    "Copyright (c) llama.cpp contributors, 2023\n" \
                    "Copyright (c) Dmitry 'sciloaf' Solovyev, 2023-2024\n"

namespace Ui {
class AboutBox;
}

class AboutBox : public QDialog
{
    Q_OBJECT

public:
    explicit AboutBox(QWidget *parent = nullptr);
    ~AboutBox();

private:
    Ui::AboutBox *ui;
};

#endif // ABOUTBOX_H
