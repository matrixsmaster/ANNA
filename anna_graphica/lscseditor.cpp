#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QStyle>
#include <QScrollBar>
#include <QPainterPath>
#include "lscseditor.h"
#include "ui_lscseditor.h"

LSCSEditor::LSCSEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LSCSEditor)
{
    ui->setupUi(this);
    ui->scroll->installEventFilter(this);
    ui->script->hide();
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
    QStatusTipEvent* ste;

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
        mev = static_cast<QMouseEvent*>(event);
        break;

    case QEvent::StatusTip:
        ste = static_cast<QStatusTipEvent*>(event);
        if (ste->tip().isEmpty()) return true;
        return false;

    default:
        break;
    }

    // standard event processing
    if (!mev) return QObject::eventFilter(obj, event);

    // mouse processing (took code from my MILLA project)
    QRect aligned = QStyle::alignedRect(QApplication::layoutDirection(),QFlag(ui->out->alignment()),img.size(),ui->out->rect());
    QRect inter = aligned.intersected(ui->out->rect());
    QPoint sd(0,0);
    if (ui->scroll->horizontalScrollBar()) sd.setX(ui->scroll->horizontalScrollBar()->value());
    if (ui->scroll->verticalScrollBar()) sd.setY(ui->scroll->verticalScrollBar()->value());

    QPoint pt(mev->x()-inter.x(),mev->y()-inter.y());
    pt += sd;
    mx = pt.x();
    my = pt.y();
    //qDebug("mx = %d\tmy = %d",mx,my);

    int pin = -1;
    AriaPod* pod = getPodUnder(mx,my,&pin);

    switch (event->type()) {
    case QEvent::MouseButtonDblClick:
        if (pod) ShowScriptFor(pod);
        break;

    case QEvent::MouseButtonPress:
        if (!(mev->modifiers() & Qt::ShiftModifier)) {
            selection.clear();
            new_link.from.clear();
        }
        if (pod) {
            new_link.from.clear();
            if (!(mev->modifiers() & Qt::ShiftModifier) && pin >= 0 && sys) {
                selection.clear();
                new_link.from = sys->getPodName(pod);
                new_link.pin_from = pin;
            } else
                selection.push_back(pod);
        }
        break;

    case QEvent::MouseButtonRelease:
        if (pod && pin >= 0 && sys && !new_link.from.empty()) {
            new_link.to = sys->getPodName(pod);
            new_link.pin_to = pin;
            NormalizeLink(new_link);
            if (!sys->Unlink(new_link)) sys->Link(new_link);
        }
        new_link.from.clear();
        break;

    case QEvent::MouseMove:
        for (auto &i : selection) {
            i->x += mx - ox;
            i->y += my - oy;
            modified = true;
        }
        break;

    default: break;
    }

    ox = mx;
    oy = my;
    Update();
    return true;
}

bool LSCSEditor::CloseIt(bool force)
{
    if (!force && (modified || script_modified)) {
        if (QMessageBox::question(this,"LSCS Editor","You have unsaved changes. Do you want to continue?",QMessageBox::No | QMessageBox::Yes,QMessageBox::No) == QMessageBox::No)
            return false;
    }

    if (sys) {
        delete sys;
        sys = nullptr;
    }

    extent = QRect(0,0,ui->scroll->width()-LCED_UI_MARGIN,ui->scroll->height()-LCED_UI_MARGIN);
    selection.clear();
    modified = false;

    ui->script->clear();
    ui->actionScript_editor->setChecked(false);
    on_actionScript_editor_triggered();
    script_fn.clear();
    script_pod.clear();
    script_modified = false;

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

    // Make a painter and font
    QPainter* p = new QPainter(&img);
    p->setBackground(QBrush(LCED_BACKGROUND));
    QFont mono("monospace");
    mono.setPixelSize(LCED_PIN_TXT_HEIGHT-LCED_PIN_TXT_VSPACE);
    p->setFont(mono);

    // Draw the dotted grid
    QRgb gcol = LCED_GRID.rgb();
    for (int i = 0; i < img.height(); i += grid) {
        QRgb* line = (QRgb*)img.scanLine(i);
        for (int j = 0; j < img.width(); j += grid, line += grid)
            *line = gcol;
    }

    // Draw all pods
    bool redraw = false;
    auto lst = sys->getPods();
    for (auto &&i : lst) {
        AriaPod* pod = sys->getPod(i);
        if (!pod) {
            qDebug("No pod to draw: %s",sys->getError().c_str());
            ui->statusbar->showMessage(QString::asprintf("Error: %s",sys->getError().c_str()));
            continue;
        }

        if (selection.contains(pod)) p->setBrush(QBrush(LCED_SELECT));
        else p->setBrush(QBrush(LCED_INFILL));

        p->setPen(LCED_BORDER);
        p->drawRect(pod->x,pod->y,pod->w,pod->h);
        p->setPen(LCED_TEXT);
        p->drawText(pod->x,pod->y-LCED_MARGIN,QString::fromStdString(i));

        if (pod->ptr) {
            p->setBrush(QBrush(LCED_INCOL));
            int n = pod->ptr->getNumInPins();
            if (n) DrawIO(p,pod->x,pod->y+LCED_PIN_DIST,pod->w,true,n,LCED_INCOL);

            p->setBrush(QBrush(LCED_OUTCOL));
            n = pod->ptr->getNumOutPins();
            if (n) DrawIO(p,pod->x+pod->w,pod->y+LCED_PIN_DIST,pod->w,false,n,LCED_OUTCOL);

            n = LCED_PIN_DIST * (std::max(pod->ptr->getNumInPins(),pod->ptr->getNumOutPins()) + 1);
            if (pod->h < n) {
                pod->h = n;
                redraw = true;
            }
        }

        if (extent.width() < pod->x + pod->w) extent.setWidth(pod->x + pod->w + LCED_MARGIN);
        if (extent.height() < pod->y + pod->h) extent.setHeight(pod->y + pod->h + LCED_MARGIN);
    }
    if (redraw) {
        modified = true; // we've modified the parameters
        delete p; // release the painter before drawing again
        Update(); // this will draw all updated pod geometries
        return;
    }

    // Draw connections
    QPen conpen(LCED_CONCOL);
    conpen.setWidth(LCED_CON_WIDTH);
    p->setPen(conpen);
    p->setBrush(QBrush(LCED_CONCOL,Qt::NoBrush));

    // Fixed connections
    for (auto &&i : lst) {
        AriaPod* pod = sys->getPod(i);
        int sx = pod->x + pod->w;
        auto links = sys->getLinksFrom(i);
        for (auto &&j : links) {
            AriaPod* recv = sys->getPod(j.to);
            if (!recv) continue;

            int sy = pod->y + (j.pin_from + 1) * LCED_PIN_DIST;
            int ey = recv->y + (j.pin_to + 1) * LCED_PIN_DIST;
            int ex = recv->x;
            DrawConnect(p,sx,sy,ex,ey);
        }
    }

    // New connection
    if (!new_link.from.empty() && new_link.pin_from >= 0) {
        AriaPod* pod = sys->getPod(new_link.from);
        if (pod && pod->ptr) {
            int sx = mx;
            int sy = my, ey = my;
            int ex = pod->x;
            if (new_link.pin_from >= pod->ptr->getNumInPins()) {
                ex = mx;
                sx = pod->x + pod->w;
                sy = pod->y + (new_link.pin_from - pod->ptr->getNumInPins() + 1) * LCED_PIN_DIST;
            } else
                ey = pod->y + (new_link.pin_from + 1) * LCED_PIN_DIST;

            DrawConnect(p,sx,sy,ex,ey);
        }
    }

    // Update the form
    ui->out->setPixmap(QPixmap::fromImage(img));
    delete p;
}

void LSCSEditor::on_actionNew_triggered()
{
    if (!CloseIt()) return;

    sys = new AnnaLSCS();
    ui->statusbar->showMessage("New LSCS system created");
    Update();
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

    Sanitize();
    Update();
}

void LSCSEditor::on_actionSave_triggered()
{
    std::string fn = QFileDialog::getSaveFileName(this,"Save LSCS file","","LSCS files (*.lscs);;All files (*.*)").toStdString();
    if (fn.empty()) return;

    if (!sys) return;
    if (sys->WriteTo(fn)) {
        ui->statusbar->showMessage(QString::asprintf("Saved to %s",fn.c_str()));
        modified = false;
    } else
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

AriaPod* LSCSEditor::getPodUnder(int x, int y, int* pin)
{
    if (!sys) return nullptr;
    auto lst = sys->getPods();
    for (auto &&i : lst) {
        AriaPod* pod = sys->getPod(i);

        if (pin && pod->ptr) {
            // look for inputs
            int px = pod->x;
            for (int q = 1; q <= pod->ptr->getNumInPins(); q++) {
                int py = pod->y + q * LCED_PIN_DIST;
                if (Distance(x,y,px,py) <= LCED_CON_HOOKRAD) {
                    *pin = q-1;
                    return pod;
                }
            }

            // look for outputs
            px = pod->x + pod->w;
            for (int q = 1; q <= pod->ptr->getNumOutPins(); q++) {
                int py = pod->y + q * LCED_PIN_DIST;
                if (Distance(x,y,px,py) <= LCED_CON_HOOKRAD) {
                    *pin = q - 1 + pod->ptr->getNumInPins();
                    return pod;
                }
            }
        }

        // match the body of the pod
        if (x >= pod->x && y >= pod->y) {
            int ex = pod->x + pod->w;
            int ey = pod->y + pod->h;
            if (x <= ex && y <= ey) return pod;
        }
    }
    return nullptr;
}

float LSCSEditor::Distance(float x0, float y0, float x1, float y1)
{
    return sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0));
}

void LSCSEditor::NormalizeLink(AriaLink& lnk)
{
    if (!sys) return;

    AriaPod* pfrom = sys->getPod(lnk.from);
    AriaPod* pto = sys->getPod(lnk.to);
    if (!pfrom || !pto || !pfrom->ptr || !pto->ptr) return;

    bool reverse = false;
    if (lnk.pin_from >= pfrom->ptr->getNumInPins())
        lnk.pin_from -= pfrom->ptr->getNumInPins();
    else
        reverse = true;

    if (lnk.pin_to >= pto->ptr->getNumInPins())
        lnk.pin_to -= pto->ptr->getNumInPins();

    if (reverse) {
        std::swap(lnk.from,lnk.to);
        std::swap(lnk.pin_from,lnk.pin_to);
    }
}

void LSCSEditor::on_actionRedraw_triggered()
{
    Update();
}

void LSCSEditor::on_actionAssign_script_triggered()
{
    if (!sys || selection.empty()) return;
    QString fn = QFileDialog::getOpenFileName(this,"Open Aria script","","Lua scripts (*.lua);;All files (*.*)");
    if (fn.isEmpty()) return;

    for (auto i : selection) {
        std::string str = sys->getPodName(i);
        if (str.empty()) {
            ui->statusbar->showMessage("Error: invalid pod in selection!");
            selection.clear();
            return;
        }
        if (!sys->setPodScript(str,fn.toStdString())) {
            ui->statusbar->showMessage(QString::asprintf("Error: unable to set script for %s: %s",str.c_str(),sys->getError().c_str()));
            return;
        }
    }

    Update();
    ui->statusbar->showMessage("Script assigned to selected pods");
}

void LSCSEditor::Sanitize()
{
    if (!sys) return;

    auto lst = sys->getPods();
    int minx = LCED_MARGIN, miny = LCED_MARGIN;

    for (auto &&i : lst) {
        AriaPod* pod = sys->getPod(i);
        if (!pod) continue;

        if (pod->x < 0) pod->x = 0;
        if (pod->x > minx) minx = pod->x;
        if (pod->y < 0) pod->y = 0;
        if (pod->y > miny) miny = pod->y;

        if (pod->w < LCED_MIN_WIDTH) pod->w = LCED_MIN_WIDTH;
        if (pod->h < LCED_MIN_HEIGHT) pod->h = LCED_MIN_HEIGHT;
    }

    /*minx -= LCED_MARGIN;
    miny -= LCED_MARGIN;
    for (auto &&i : lst) {
        AriaPod* pod = sys->getPod(i);
        if (!pod) continue;
        if (minx > 0) pod->x -= minx;
        if (miny > 0) pod->y -= miny;
    }*/
}

void LSCSEditor::on_actionSanitize_triggered()
{
    Sanitize();
    Update();
}

void LSCSEditor::DrawIO(QPainter *p, int sx, int sy, int w, bool input, int num, QColor col)
{
    QPoint arr[LCED_SYM_CONNECT_N];
    int darr[] = LCED_SYM_CONNECT;

    for (int n = 0; n < num; n++) {
        for (int i = 0; i < LCED_SYM_CONNECT_N; i++) {
            arr[i].setX(sx + darr[i*2]);
            arr[i].setY(sy + darr[i*2+1]);
        }

        p->setPen(col);
        p->drawPolygon(arr,LCED_SYM_CONNECT_N);

        QString txt = QString::asprintf("%d",n);
        QRect rct;
        if (input) rct = QRect(QPoint(sx+LCED_PIN_TXT_MARGIN,sy-LCED_PIN_TXT_HEIGHT/2),QPoint(sx+w/2,sy+LCED_PIN_TXT_HEIGHT/2));
        else rct = QRect(QPoint(sx-w/2,sy-LCED_PIN_TXT_HEIGHT/2),QPoint(sx-LCED_PIN_TXT_MARGIN,sy+LCED_PIN_TXT_HEIGHT/2));
        p->setPen(LCED_TEXT);
        p->drawText(rct,(input? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,txt);

        sy += LCED_PIN_DIST;
    }
}

void LSCSEditor::DrawConnect(QPainter* p, int sx, int sy, int ex, int ey)
{
    int dx = (ex-sx) / 2;

    QPainterPath pth;
    pth.moveTo(sx,sy);
    if (ex >= sx)
        pth.cubicTo(QPointF(sx+dx,sy),QPointF(sx+dx,ey),QPointF(ex,ey));
    else
        pth.cubicTo(QPointF(sx-dx,sy),QPointF(ex+dx,ey),QPointF(ex,ey));

    p->drawPath(pth);
}

void LSCSEditor::on_actionScript_editor_triggered()
{
    if (ui->actionScript_editor->isChecked()) ui->script->show();
    else ui->script->hide();
    ui->menuScript->setEnabled(ui->actionScript_editor->isChecked());
}

void LSCSEditor::ShowScriptFor(AriaPod* pod)
{
    if (!pod || !sys) return;

    // show script editor
    ui->actionScript_editor->setChecked(true);
    on_actionScript_editor_triggered();

    // load script or create a new one
    if (!NewScript()) return;
    if (!pod->ptr || pod->ptr->getFName().empty()) {
        // new script
        script_fn = QFileDialog::getSaveFileName(this,"New Aria script","","Lua scripts (*.lua);;All files (*.*)");
        if (script_fn.isEmpty()) return;

    } else {
        // load script
        script_fn = QString::fromStdString(pod->ptr->getFName());
        LoadScript();
    }

    script_pod = QString::fromStdString(sys->getPodName(pod));
    script_modified = false;
}

bool LSCSEditor::NewScript()
{
    if (script_modified) {
        if (QMessageBox::question(this,"LSCS Editor","You have unsaved changes in current active script. Do you want to continue?",QMessageBox::No | QMessageBox::Yes,QMessageBox::No) == QMessageBox::No)
            return false;
    }

    ui->script->clear();
    script_fn.clear();
    script_pod.clear();
    script_modified = false;

    return true;
}

void LSCSEditor::LoadScript()
{
    QFile sf(script_fn);
    if (!sf.exists() || !sf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui->statusbar->showMessage("Error: unable to open file " + script_fn);
        return;
    }

    QString data = QString::fromStdString(sf.readAll().toStdString());
    sf.close();

    ui->script->setPlainText(data);
    ui->statusbar->showMessage("Loaded script from " + script_fn);
    script_modified = false;
}

void LSCSEditor::SaveScript()
{
    QFile sf(script_fn);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ui->statusbar->showMessage("Error: unable to write to " + script_fn);
        return;
    }

    QByteArray data = QByteArray::fromStdString(ui->script->toPlainText().toStdString());
    sf.write(data);
    sf.close();

    ui->statusbar->showMessage("Saved script to " + script_fn);
    script_modified = false;
}

void LSCSEditor::on_actionSave_2_triggered()
{
    if (script_fn.isEmpty()) return;
    SaveScript();

    // try to reload the pod
    if (!sys || script_pod.isEmpty()) return;
    if (!sys->setPodScript(script_pod.toStdString(),script_fn.toStdString()))
        ui->statusbar->showMessage(QString::asprintf("Error: unable to set script: %s",sys->getError().c_str()));

    Update();
}

void LSCSEditor::on_actionLoad_2_triggered()
{
    if (!NewScript()) return;

    script_fn = QFileDialog::getOpenFileName(this,"Open Aria script","","Lua scripts (*.lua);;All files (*.*)");
    if (!script_fn.isEmpty()) LoadScript();
}
