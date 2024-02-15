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
    QRegExp bex, eex;
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

    void on_pushButton_clicked();

private:
    Ui::RQPEditor *ui;
    QSettings *sets;

    void rescan();
    void sync();

    static QStringList CompleteRQP(const QString& in, AnnaRQPState* st);
};

#endif // RQPEDITOR_H
