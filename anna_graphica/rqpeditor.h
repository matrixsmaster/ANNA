#ifndef RQPEDITOR_H
#define RQPEDITOR_H

#include <QDialog>
#include <QSettings>

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

protected:
    virtual void showEvent(QShowEvent *event) override;

private slots:
    void on_testEdit_textChanged();

    void on_buttonBox_accepted();

private:
    Ui::RQPEditor *ui;
    QSettings *sets;

    void rescan();
};

#endif // RQPEDITOR_H
