#include "mainwindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    const QStringList args = a.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i).startsWith(QLatin1Char('-')))
            continue;
        const QString path = args.at(i);
        QTimer::singleShot(0, &w, [&w, path]() { w.loadVideo(path); });
        break;
    }

    return a.exec();
}
