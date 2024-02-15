#ifndef RQPEDITOR_H
#define RQPEDITOR_H

#include <algorithm>
#include <functional>
#include <QDialog>
#include <QSettings>
#include <QProcess>

#define ANNA_PROCESS_IO_BUFLEN 1024
#define ANNA_ARGPARSE_FAILSAFE 100

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

    static QStringList DetectRQP(const QString& in, AnnaRQPState& st);
    static QString DoRequest(AnnaRQPState& rqp, const QString& inp, bool do_events, std::function<void(QString&)> notify);

    QString filename;

protected:
    virtual void showEvent(QShowEvent *event) override;

private slots:
    void on_testEdit_textChanged();

    void on_buttonBox_accepted();

    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

private:
    Ui::RQPEditor *ui;
    QSettings *sets;

    void rescan();
    void sync();

    static QStringList CompleteRQP(const QString& in, AnnaRQPState& st);
};

#endif // RQPEDITOR_H
