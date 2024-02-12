#ifndef RQPEDITOR_H
#define RQPEDITOR_H

#include <QDialog>

namespace Ui {
class RQPEditor;
}

class RQPEditor : public QDialog
{
    Q_OBJECT

public:
    explicit RQPEditor(QWidget *parent = nullptr);
    ~RQPEditor();

    QString filename;

private:
    Ui::RQPEditor *ui;
};

#endif // RQPEDITOR_H
