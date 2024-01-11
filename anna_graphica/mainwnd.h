#ifndef MAINWND_H
#define MAINWND_H

#include <QMainWindow>
#include <QFileDialog>
#include <QMessageBox>
#include "brain.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWnd; }
QT_END_NAMESPACE

class TextBoxKeyFilter : public QObject
{
    Q_OBJECT
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

class MainWnd : public QMainWindow
{
    Q_OBJECT

public:
    MainWnd(QWidget *parent = nullptr);
    ~MainWnd();

    void LoadLLM(const QString& fn);
    void ForceAIName(const QString& nm);
    void Generate();

private slots:
    void on_actionSimple_view_triggered();

    void on_actionAdvanced_view_triggered();

    void on_actionProfessional_view_triggered();

    void on_ModelFindButton_clicked();

    void on_SendButton_clicked();

    void on_AttachButton_clicked();

    void on_AINameBox_currentIndexChanged(const QString &arg1);

    void on_actionLoad_model_triggered();

private:
    Ui::MainWnd *ui;
    AnnaBrain* brain;
    //std::string dialog;
};
#endif // MAINWND_H
