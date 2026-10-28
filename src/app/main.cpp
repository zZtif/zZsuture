#include <QFile>
#include <QIcon>
#include <QTranslator>
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

    // Qt 自带控件的中文化（QMessageBox 的 Yes/No 按钮、标准对话框文字等；
    // 界面自身文字都是代码里的中文，不经翻译）。windeployqt 已把
    // qt_zh_CN.qm 部署到 exe 旁的 translations/。加载失败退英文（非致命）
    auto* qtTr = new QTranslator(&app);
    if (qtTr->load(QLocale::system(), QStringLiteral("qt_zh_CN"), QStringLiteral("_"),
                   QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
        QCoreApplication::installTranslator(qtTr);

    // 暗色主题：QSS 打包在 qrc 中（架构 §5.9）。
    // 教训（AUTORCC 事故）：加载失败曾被 if 守卫静默吞掉，界面退默认浅色
    // 无人察觉——失败必须出声
    QFile qss(QStringLiteral(":/theme.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    } else {
        qWarning("theme.qss 加载失败——界面将使用 Qt 默认样式");
    }

    suture::MainWindow win;
    win.show();
    return app.exec();
}
