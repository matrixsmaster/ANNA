#ifndef RQPEDITOR_H
#define RQPEDITOR_H

#include <QDialog>
#include <QSettings>

namespace Ui {
class RQPEditor;
}

struct AnnaRQPFile {
    QString fn;
    bool enabled;
};

struct AnnaRQPState {
    QSettings* s;
    int fsm, lpos;
};

class RQPEditor : public QDialog
{
    Q_OBJECT

public:
    explicit RQPEditor(QWidget *parent = nullptr);
    ~RQPEditor();

    static QStringList DetectRQP(const QString& in, AnnaRQPState* st);

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
