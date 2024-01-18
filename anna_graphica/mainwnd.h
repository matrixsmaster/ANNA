#ifndef MAINWND_H
#define MAINWND_H

#include <QMainWindow>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFile>
#include <QTextStream>
#include <QListWidget>
#include <QSettings>
#include "brain.h"

#define ANNA_DEFAULT_CONTEXT 4096
#define ANNA_DEFAULT_BATCH 512
#define ANNA_DEFAULT_TEMP 0.3
#define ANNA_DEFAULT_PROMPT "SYSTEM: You're a helpful AI assistant named Anna. You're helping your user with their daily tasks.\n"
#define ANNA_CONFIG_FILE "anna.cfg"
#define GUI_ICON_W 48
#define GUI_ICON_H 48

struct AnnaAttachment {
    QString fn;
    QString shrt;
    QPixmap pic;
    QString txt;
    QListWidgetItem* itm;
};

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
    void LoadSettings();
    void SaveSettings();
    bool LoadFile(const QString& fn, QString& str);
    bool SaveFile(const QString& fn, const QString& str);
    void LoadLLM(const QString& fn);
    void ForceAIName(const QString& nm);
    void ProcessInput(std::string str);
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

    void on_actionClear_attachments_triggered();

    void on_actionLoad_vision_encoder_triggered();

    void on_actionPlain_text_triggered();

    void on_actionAbout_triggered();

    void on_pushButton_clicked();

    void on_actionHTML_triggered();

    void on_actionSave_state_triggered();

    void on_actionLoad_state_triggered();

    void on_actionShow_prompt_triggered();

private:
    Ui::MainWnd *ui;
    QLabel* seed_label;

    AnnaConfig config;
    AnnaBrain* brain;

    std::list<AnnaAttachment> attachs;
    AnnaAttachment* next_attach;
    bool last_username;
    bool stop;
};
#endif // MAINWND_H
