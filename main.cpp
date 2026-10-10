#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include <cstring>

#ifdef VFC_HAS_SELFTEST
#include "selftest.h"
#endif

int main(int argc, char *argv[])
{
#ifdef VFC_HAS_SELFTEST
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) {
            // 让 qDebug/qInfo 走 stderr，Qt Creator「应用程序输出」才能收到
            qputenv("QT_FORCE_STDERR_LOGGING", "1");
            qputenv("QT_LOGGING_TO_CONSOLE", "1");
            break;
        }
    }
#endif
    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    const QStringList args = a.arguments();

#ifdef VFC_HAS_SELFTEST
    if (args.contains(QLatin1String("--selftest"))) {
        QTimer::singleShot(0, &w, [&w]() {
            ISelfTest *test = createSelfTest();
            const int code = test->run(&w);
            delete test;
            QCoreApplication::exit(code);
        });
        return a.exec();
    }
#endif

    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i).startsWith(QLatin1Char('-')))
            continue;
        const QString path = args.at(i);
        QTimer::singleShot(0, &w, [&w, path]() { w.loadVideo(path); });
        break;
    }

    return a.exec();
}
