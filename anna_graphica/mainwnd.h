#ifndef MAINWND_H
#define MAINWND_H

#include <QMainWindow>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFile>
#include <QTextStream>
#include "brain.h"

#define ANNA_DEFAULT_PROMPT "SYSTEM: You're a helpful AI assistant named Anna. You're helping your user with their daily tasks.\n"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWnd; }
QT_END_NAMESPACE

class MainWnd : public QMainWindow
{
    Q_OBJECT

public:
    MainWnd(QWidget *parent = nullptr);
    ~MainWnd();

    void DefaultConfig();
    bool LoadFile(const QString& fn, QString& str);
    bool SaveFile(const QString& fn, const QString& str);
    void LoadLLM(const QString& fn);
    void ForceAIName(const QString& nm);
    void Generate();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void on_actionSimple_view_triggered();

    void on_actionAdvanced_view_triggered();

    void on_actionProfessional_view_triggered();

    void on_ModelFindButton_clicked();

    void on_SendButton_clicked();

    void on_AttachButton_clicked();

    void on_actionLoad_model_triggered();

    void on_actionNew_dialog_triggered();

    void on_actionQuit_triggered();

    void on_actionMarkdown_triggered();

    void on_actionLoad_initial_prompt_triggered();

    void on_actionSettings_triggered();

private:
    Ui::MainWnd *ui;
    AnnaConfig config;
    AnnaBrain* brain;

    bool last_username;
};
#endif // MAINWND_H
