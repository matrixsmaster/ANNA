#include "mainwnd.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWnd w;
    w.show();
    return a.exec();
}
