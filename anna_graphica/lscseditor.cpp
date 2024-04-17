#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>
#include "lscseditor.h"
#include "ui_lscseditor.h"

LSCSEditor::LSCSEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LSCSEditor)
{
    ui->setupUi(this);
}

LSCSEditor::~LSCSEditor()
{
    CloseIt(true);
    delete ui;
}

void LSCSEditor::closeEvent(QCloseEvent *event)
{
    if (CloseIt()) event->accept();
    else event->ignore();
}

bool LSCSEditor::CloseIt(bool force)
{
    if (!force && modified) {
        if (QMessageBox::question(this,"LSCS Editor","You have unsaved changes. Do you want to continue?",QMessageBox::No | QMessageBox::Yes,QMessageBox::No) == QMessageBox::No)
            return false;
    }

    if (sys) {
        delete sys;
        sys = nullptr;
    }

    extent = QRect(0,0,ui->scroll->width(),ui->scroll->height());
    img = QImage(extent.width(),extent.height(),QImage::Format_RGB32);

    modified = false;
    return true;
}

void LSCSEditor::Update()
{
    //TODO
    ui->out->setPixmap(QPixmap::fromImage(img));
}

void LSCSEditor::on_actionNew_triggered()
{
    if (!CloseIt()) return;

    sys = new AnnaLSCS();
}

void LSCSEditor::on_actionLoad_triggered()
{
    if (!CloseIt()) return;

    std::string fn = QFileDialog::getOpenFileName(this,"Open LSCS file","","LSCS files (*.lscs);;All files (*.*)").toStdString();
    if (fn.empty()) return;

    sys = new AnnaLSCS(fn);
    if (sys->getState() != ANNA_READY) {
        ui->statusbar->showMessage(QString::asprintf("Unable to load file %s",fn.c_str()));
        delete sys;
        sys = nullptr;
    } else
        ui->statusbar->showMessage(QString::asprintf("Loaded from %s",fn.c_str()));

    Update();
}

void LSCSEditor::on_actionSave_triggered()
{
    std::string fn = QFileDialog::getSaveFileName(this,"Save LSCS file","","LSCS files (*.lscs);;All files (*.*)").toStdString();
    if (fn.empty()) return;

    if (!sys) return;
    if (sys->WriteTo(fn))
        ui->statusbar->showMessage(QString::asprintf("Saved to %s",fn.c_str()));
    else
        ui->statusbar->showMessage(QString::asprintf("Unable to save file %s",fn.c_str()));
}

void LSCSEditor::on_actionClose_triggered()
{
    if (CloseIt()) accept();
}
