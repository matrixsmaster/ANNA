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
#define LCED_PIN_TXT_DX 8
#define LCED_PIN_TXT_DY 4
#define LCED_CON_WIDTH 2

#define LCED_BACKGROUND QColor(0,0,0)
#define LCED_GRID QColor(50,50,50)
#define LCED_TEXT QColor(250,250,250)
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

private slots:
    void on_actionNew_triggered();

    void on_actionLoad_triggered();

    void on_actionSave_triggered();

    void on_actionClose_triggered();

    void on_actionAdd_pod_triggered();

    void on_actionRedraw_triggered();

    void on_actionAssign_script_triggered();

    void on_actionSanitize_triggered();

    void on_actionScript_editor_triggered();

    void on_actionSave_2_triggered();

    void on_actionLoad_2_triggered();

private:
    Ui::LSCSEditor *ui;
    AnnaLSCS* sys = nullptr;
    QImage img;
    QRect extent;
    bool modified = false;
    int mx = 0, my = 0;
    int ox = 0, oy = 0;
    int grid = LCED_DEF_GRID;
    QList<AriaPod*> selection;
    QString script_fn, script_pod;
    bool script_modified = false;

    AriaPod* getPodUnder(int x, int y);
    void Sanitize();
    void DrawIO(QPainter* p, int sx, int sy, int dtx, int num, QColor col);
    void ShowScriptFor(AriaPod* pod);
    bool NewScript();
    void LoadScript();
    void SaveScript();
};

#endif // LSCSEDITOR_H
