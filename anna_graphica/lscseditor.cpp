#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QPainter>
#include <QStyle>
#include <QScrollBar>
#include "lscseditor.h"
#include "ui_lscseditor.h"

LSCSEditor::LSCSEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LSCSEditor)
{
    ui->setupUi(this);
    ui->scroll->installEventFilter(this);
}

LSCSEditor::~LSCSEditor()
{
    CloseIt(true);
    delete ui;
}

void LSCSEditor::closeEvent(QCloseEvent *event)
{
    if (CloseIt()) {
        event->accept();
        accept();
    } else
        event->ignore();
}

bool LSCSEditor::eventFilter(QObject* obj, QEvent* event)
{
    QMouseEvent* mev = nullptr;

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        mev = static_cast<QMouseEvent*>(event);
        break;

    case QEvent::StatusTip:
    {
        QStatusTipEvent* se = static_cast<QStatusTipEvent*>(event);
        if (se->tip().isEmpty()) return true;
        return false;
    }

    default:
        break;
    }

    // mouse processing (took code from my MILLA project)
    QRect aligned = QStyle::alignedRect(QApplication::layoutDirection(),QFlag(ui->out->alignment()),img.size(),ui->out->rect());
    QRect inter = aligned.intersected(ui->out->rect());
    QPoint scrl_delta(0,0);
    if (ui->scroll->horizontalScrollBar()) scrl_delta.setX(ui->scroll->horizontalScrollBar()->value());
    if (ui->scroll->verticalScrollBar()) scrl_delta.setY(ui->scroll->verticalScrollBar()->value());

    if (event->type() == QEvent::MouseMove) {
        QPoint pt(mev->x()-inter.x(),mev->y()-inter.y());
        pt += scrl_delta;
        mx = pt.x();
        my = pt.y();
        qDebug("mx = %d\tmy = %d",mx,my);

        AriaPod* ptr = getPodUnder(mx,my);
        if (ptr) {
            //TODO: check for shift key
            selection.clear();
            selection.push_back(ptr);
        }
    }

    if (mev) {
        Update();
        return true;
    }

    // standard event processing
    return QObject::eventFilter(obj, event);
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
    //img = QImage(extent.width(),extent.height(),QImage::Format_RGB32);

    selection.clear();

    modified = false;
    return true;
}

void LSCSEditor::Update()
{
    img = QImage(extent.width(),extent.height(),QImage::Format_RGB32);
    img.fill(LCED_BACKGROUND);

    if (!sys) {
        ui->out->setPixmap(QPixmap::fromImage(img));
        return;
    }

    QPainter p(&img);
    p.setBackground(QBrush(LCED_BACKGROUND));
    //p.setBrush(QBrush(LCED_INFILL));
    p.setPen(LCED_BORDER);

    auto lst = sys->getPods();
    for (auto &&i : lst) {
        AriaPod* pod = sys->getPod(i);
        if (!pod) {
            qDebug("No pod to draw: %s",sys->getError().c_str());
            ui->statusbar->showMessage(QString::asprintf("Error: %s",sys->getError().c_str()));
            continue;
        }

        if (selection.contains(pod)) p.setBrush(QBrush(LCED_SELECT));
        else p.setBrush(QBrush(LCED_INFILL));

        p.drawRect(pod->x,pod->y,pod->w,pod->h);
        p.drawText(pod->x,pod->y-LCED_MARGIN,QString::fromStdString(i));
    }

    ui->out->setPixmap(QPixmap::fromImage(img));
}

void LSCSEditor::on_actionNew_triggered()
{
    if (!CloseIt()) return;

    sys = new AnnaLSCS();
    ui->statusbar->showMessage("New LSCS system created");
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

void LSCSEditor::on_actionAdd_pod_triggered()
{
    if (!sys) return;

    bool ok = false;
    QString text = QInputDialog::getText(this,"Creating new Aria pod","Enter new unique name for the pod:",QLineEdit::Normal,"",&ok);
    if (!ok || text.isEmpty()) return;

    AriaPod* ptr = sys->addPod(text.toStdString());
    if (!ptr) {
        ui->statusbar->showMessage(QString::asprintf("Error: %s",sys->getError().c_str()));
        return;
    }
    ptr->x = mx;
    ptr->y = my;
    ptr->w = LCED_MIN_WIDTH;
    ptr->h = LCED_MIN_HEIGHT;

    selection.clear();
    modified = true;
    Update();
}

AriaPod* LSCSEditor::getPodUnder(int x, int y)
{
    if (!sys) return nullptr;
    auto lst = sys->getPods();
    for (auto &&i : lst) {
        AriaPod* pod = sys->getPod(i);
        if (x >= pod->x && y >= pod->y) {
            int ex = pod->x + pod->w;
            int ey = pod->y + pod->h;
            if (x <= ex && y <= ey) return pod;
        }
    }
    return nullptr;
}
