#include <QMessageBox>
#include <QCloseEvent>
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
    //TODO
}

void LSCSEditor::on_actionSave_triggered()
{
    //TODO
}

void LSCSEditor::on_actionClose_triggered()
{
    if (CloseIt()) accept();
}
