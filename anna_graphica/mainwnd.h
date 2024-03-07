#ifndef MAINWND_H
#define MAINWND_H

#include <QMainWindow>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFile>
#include <QTextStream>
#include <QListWidget>
#include <QComboBox>
#include <QSettings>
#include <QCompleter>
#include <mutex>
#include "rqpeditor.h"
#include "busybox.h"
#include "../brain.h"
#include "../netclient.h"

#define AG_VERSION "0.9.1b"

#define AG_MAXTEXT 10*1024*1024
#define AG_ICON_W 48
#define AG_ICON_H 48
#define AG_DEFAULT_CONTEXT 4096
#define AG_DEFAULT_BATCH 512
#define AG_DEFAULT_TEMP 0.3
#define AG_DEFAULT_PROMPT "SYSTEM: You're a helpful AI assistant named Anna. You're helping your user with their daily tasks.\n"
#define AG_CONFIG_FILE "anna.cfg"
#define AG_QUICK_FILE "quicksave.anna"
#define AG_MDFIX_FAILSAFE 100000
#define AG_DEFAULT_SERVER "127.0.0.1:8080"
#define AG_SERVER_WAIT_MS 50
#define AG_SERVER_WAIT_CYCLES 20
#define AG_SERVER_KEEPALIVE_MINS 2

struct AnnaAttachment {
    QString fn;
    QString shrt;
    QPixmap pic;
    QString txt;
    QListWidgetItem* itm;
};

struct AnnaGuiSettings {
    int enter_key;
    bool md_fix, save_prompt, clear_log;
    QString server;
    bool use_server, use_busybox, use_attprefix, mk_dummy;
    QFont log_fnt, usr_fnt;
    QString att_prefix, txt_prefix, txt_suffix;
    std::vector<AnnaRQPFile> rqps;
};

enum AnnaFileDialogType {
    ANNA_FILE_LLM,
    ANNA_FILE_PROMPT,
    ANNA_FILE_DLG_TXT,
    ANNA_FILE_DLG_MD,
    ANNA_FILE_DLG_HTML,
    ANNA_FILE_MODEL_STATE,
    ANNA_FILE_CLIP,
    ANNA_FILE_ATTACHMENT,
    ANNA_NUM_FILETYPES
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

protected:
    void closeEvent(QCloseEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
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

    void on_AttachmentsList_itemDoubleClicked(QListWidgetItem *item);

    void on_actionRefresh_chat_box_triggered();

    void on_actionShow_context_tokens_triggered();

    void on_actionQuick_save_triggered();

    void on_actionQuick_load_triggered();

    void on_actionOffline_help_triggered();

    void on_actionClear_chat_log_triggered();

    void on_actionUse_current_input_as_prompt_triggered();

    void on_actionReset_prompt_to_default_triggered();

private:
    Ui::MainWnd *ui;
    QLabel* seed_label;
    BusyBox* busy_box;

    AnnaConfig config;
    AnnaGuiSettings guiconfig;
    AnnaBrain* brain;

    int mode;
    QString cur_chat, raw_output;
    std::list<AnnaAttachment> attachs;
    AnnaAttachment* next_attach;
    bool last_username;
    bool stop;
    bool block;
    QString filedlg_cache[ANNA_NUM_FILETYPES];
    std::vector<AnnaRQPState> rqps;
    std::mutex busybox_lock;
    bool nowait;

    void DefaultConfig();
    void LoadSettings();
    void SaveSettings();
    bool LoadFile(const QString& fn, QString& str);
    bool SaveFile(const QString& fn, const QString& str);
    bool NewBrain();
    void LoadLLM(const QString& fn);
    bool LoadLLMState(const QString& fn);
    void FixMarkdown(QString& s);
    void UpdateRQPs();
    void CheckRQPs(const QString& inp);
    void ForceAIName(const QString& nm);
    void ProcessInput(std::string str);
    bool EmbedImage(const QString& fn);
    void Generate();

    QString GetSaveFileName(const AnnaFileDialogType tp);
    QString GetOpenFileName(const AnnaFileDialogType tp);
    void SaveComboBox(QSettings* sets, QString prefix, QComboBox* box);
    void LoadComboBox(QSettings* sets, QString prefix, QComboBox* box);

    bool WaitingFun(int prog, bool wait, const std::string& text = "");
};

#endif // MAINWND_H
