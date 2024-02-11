#include "mainwnd.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    srand(time(NULL));
    QApplication a(argc, argv);
    MainWnd w;
    w.show();
    return a.exec();
}
