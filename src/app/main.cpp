#include <QFile>
#include <QIcon>
#include <QtWidgets/QApplication>

#include "mainwindow.hpp"
#include "version.hpp"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SUture"));
    QApplication::setApplicationVersion(QString::fromUtf8(suture::kVersionString));
    QApplication::setOrganizationName(QStringLiteral("SUture"));
    // PNG 资源：QtGui 内建解码，裸 exe（无 imageformats 插件）也能显示窗口
    // 图标——ICO 依赖 qico.dll 插件，直接跑构建目录的 exe 时插件不在场
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.png")));

    // 暗色主题：QSS 打包在 qrc 中（架构 §5.9）
    QFile qss(QStringLiteral(":/theme.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    suture::MainWindow win;
    win.show();
    return app.exec();
}
