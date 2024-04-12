#ifndef RQPEDITOR_H
#define RQPEDITOR_H

#include <algorithm>
#include <functional>
#include <QDialog>
#include <QSettings>
#include <QProcess>

#define AG_PROCESS_WAIT_US 10000UL
#define AG_ARGPARSE_FAILSAFE 100

typedef std::function<void(QString&)> AnnaRQPNotify;
typedef std::function<bool(QString&,QStringList&)> AnnaRQPFilter;

struct AnnaRQPFile {
    QString fn;
    bool enabled;
};

struct AnnaRQPState {
    QSettings* s;
    int fsm, lpos;
    QRegExp bex, eex;
};

namespace Ui {
class RQPEditor;
}

class RQPEditor : public QDialog
{
    Q_OBJECT

public:
    explicit RQPEditor(QWidget *parent = nullptr);
    ~RQPEditor();

    static QStringList DetectRQP(const QString& in, AnnaRQPState& st);
    static QString DoRequest(AnnaRQPState& rqp, const QString& inp, bool do_events, AnnaRQPFilter filter, AnnaRQPNotify notify);

    QString filename;

protected:
    virtual void showEvent(QShowEvent *event) override;

private slots:
    void on_testEdit_textChanged();

    void on_RQPEditor_accepted();

    void on_RQPEditor_rejected();

    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

private:
    Ui::RQPEditor *ui;
    QSettings *sets, *backup;
    QString prev_test;

    void rescan();
    void sync();

    static std::list<QString> splitter(const QString& str);
    static void replacer(QString& str, const QString& tag, const QString& in);

    static QStringList CompleteRQP(const QString& in, AnnaRQPState& st);
};

#endif // RQPEDITOR_H
