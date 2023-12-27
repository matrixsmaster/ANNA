#ifndef MAINWND_H
#define MAINWND_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWnd; }
QT_END_NAMESPACE

class MainWnd : public QMainWindow
{
    Q_OBJECT

public:
    MainWnd(QWidget *parent = nullptr);
    ~MainWnd();

private slots:
    void on_actionSimple_view_triggered();

    void on_actionAdvanced_view_triggered();

    void on_actionProfessional_view_triggered();

private:
    Ui::MainWnd *ui;
};
#endif // MAINWND_H
