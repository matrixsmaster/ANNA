#ifndef LSCSEDITOR_H
#define LSCSEDITOR_H

#include <QDialog>
#include <QPixmap>
#include <QImage>
#include <QColor>
#include <QPainter>
#include "lscs.h"

#define LCED_MARGIN 8
#define LCED_UI_MARGIN 4
#define LCED_PIN_DIST 16
#define LCED_MIN_WIDTH 70
#define LCED_MIN_HEIGHT 40
#define LCED_DEF_GRID 8
#define LCED_PIN_TXT_MARGIN 8
#define LCED_PIN_TXT_HEIGHT 16
#define LCED_PIN_TXT_VSPACE 4
#define LCED_CON_WIDTH 2
#define LCED_CON_HOOKRAD 9

#define LCED_BACKGROUND QColor(0,0,0)
#define LCED_GRID QColor(50,50,50)
#define LCED_TEXT QColor(250,250,250)
#define LCED_DBGTEXT QColor(190,190,190)
#define LCED_BORDER QColor(180,0,200)
#define LCED_INFILL QColor(100,100,100)
#define LCED_SELECT QColor(200,100,200)
#define LCED_INCOL QColor(0,200,20)
#define LCED_OUTCOL QColor(150,20,50)
#define LCED_CONCOL QColor(100,100,250)

#define LCED_SYM_CONNECT_N 7
#define LCED_SYM_CONNECT { 0,0, -4,-3, 4,-3, 6,0, 4,3, -4,3, 0,0 }

namespace Ui {
class LSCSEditor;
}

class LSCSEditor : public QDialog
{
    Q_OBJECT

public:
    explicit LSCSEditor(QWidget *parent = nullptr);
    ~LSCSEditor();

    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

    bool CloseIt(bool force = false);
    void Update();

    void setSplitter(const QByteArray & arr);
    QByteArray getSplitter();

private slots:
    void on_actionNew_triggered();

    void on_actionLoad_triggered();

    void on_actionSave_triggered();

    void on_actionSave_as_triggered();

    void on_actionClose_triggered();

    void on_actionAdd_pod_triggered();

    void on_actionRedraw_triggered();

    void on_actionAssign_script_triggered();

    void on_actionSanitize_triggered();

    void on_actionScript_editor_triggered();

    void on_actionSave_2_triggered();

    void on_actionLoad_2_triggered();

    void on_actionRemove_pod_triggered();

    void on_actionDry_run_triggered();

    void on_actionRename_pod_triggered();

    void on_actionApply_input_triggered();

    void on_actionShrink_pods_triggered();

    void on_actionReset_triggered();

private:
    Ui::LSCSEditor *ui;
    AnnaLSCS* sys = nullptr;
    QImage img;
    QRect extent;
    bool modified = false;
    QString system_fn;
    int mx = 0, my = 0;
    int ox = 0, oy = 0;
    int grid = LCED_DEF_GRID;
    QList<AriaPod*> selection;
    AriaLink new_link;
    QString script_fn, script_pod;
    bool script_modified = false;
    QString debug_text;
    QPoint debug_txtpos;

    AriaPod* getPodUnder(int x, int y, int* pin = nullptr);
    float Distance(float x0, float y0, float x1, float y1);
    void NormalizeLink(AriaLink& lnk);
    void Sanitize();
    bool DrawIO(QPainter *p, QStringList names, int sx, int sy, int w, bool input, QColor col);
    void DrawConnect(QPainter* p, int sx, int sy, int ex, int ey);
    void ShowScriptFor(AriaPod* pod);
    bool NewScript();
    void LoadScript();
    void SaveScript();
};

#endif // LSCSEDITOR_H
