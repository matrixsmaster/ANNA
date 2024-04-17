#ifndef LSCSEDITOR_H
#define LSCSEDITOR_H

#include <QDialog>
#include <QPixmap>
#include <QImage>
#include <QColor>
#include "lscs.h"

#define LCED_MARGIN 8
#define LCED_PIN_DIST 16
#define LCED_MIN_WIDTH 32
#define LCED_MIN_HEIGHT 32
#define LCED_DEF_GRID 8
#define LCED_BACKGROUND QColor(0,0,0)
#define LCED_GRID QColor(50,50,50)
#define LCED_BORDER QColor(180,0,200)
#define LCED_INFILL QColor(100,100,100)
#define LCED_SELECT QColor(200,100,200)

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

    AriaPod* getPodUnder(int x, int y);
};

#endif // LSCSEDITOR_H
