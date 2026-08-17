#include "canvasview.h"
#include "mainwindow.h"
#include "selftest.h"

#include <QApplication>
#include <QByteArray>
#include <QString>
#include <QStringList>

#include <QtGlobal>

int main(int argc, char *argv[])
{
    // 解析命令行：--selftest <输出.png> 进入离屏自测；其余参数视为图片路径。
    bool selftest = false;
    QString outPath;
    QStringList files;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--selftest")) {
            selftest = true;
        } else if (selftest && outPath.isEmpty()) {
            outPath = arg;
        } else {
            files << arg;
        }
    }

    if (selftest)
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("dental-composer"));
    QApplication::setOrganizationName(QStringLiteral("dental"));

    if (selftest) {
        if (outPath.isEmpty())
            outPath = QStringLiteral("selftest.png");
        return runSelftest(outPath);
    }

    MainWindow window;
    window.show();
    if (!files.isEmpty())
        window.canvas()->importImages(files);
    return app.exec();
}
