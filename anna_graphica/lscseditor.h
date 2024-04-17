#ifndef LSCSEDITOR_H
#define LSCSEDITOR_H

#include <QDialog>
#include <QPixmap>
#include <QImage>
#include "lscs.h"

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

    bool CloseIt(bool force = false);
    void Update();

private slots:
    void on_actionNew_triggered();

    void on_actionLoad_triggered();

    void on_actionSave_triggered();

    void on_actionClose_triggered();

private:
    Ui::LSCSEditor *ui;
    AnnaLSCS* sys = nullptr;
    QImage img;
    QRect extent;
    bool modified = false;
};

#endif // LSCSEDITOR_H
