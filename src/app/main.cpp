#include <QFile>
#include <QtWidgets/QApplication>

#include "mainwindow.hpp"
#include "version.hpp"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SUture"));
    QApplication::setApplicationVersion(QString::fromUtf8(suture::kVersionString));
    QApplication::setOrganizationName(QStringLiteral("SUture"));

    // 暗色主题：QSS 打包在 qrc 中（架构 §5.9）
    QFile qss(QStringLiteral(":/theme.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    suture::MainWindow win;
    win.show();
    return app.exec();
}
