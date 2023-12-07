#include "mainwnd.h"
#include "ui_mainwnd.h"

MainWnd::MainWnd(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWnd)
{
    ui->setupUi(this);
}

MainWnd::~MainWnd()
{
    delete ui;
}

