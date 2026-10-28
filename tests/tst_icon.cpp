// tst_icon —— 应用图标资源回归测试
// 起因（2026-10-28）：窗口图标在打包 exe 上不生效（WM_GETICON 全 0），
// 用本测试钉死"资源在 qrc 里且可解码"这一环，与运行环境解耦。
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QPixmap>
#include <QtTest>

class TestIcon : public QObject {
    Q_OBJECT
private slots:
    void dumpRootResources()     // 诊断：列出 :/ 根下实际注册了什么
    {
        qInfo() << "root:" << QDir(":/").entryList();
        qInfo() << "exists :/theme.qss" << QFile::exists(":/theme.qss");
        qInfo() << "exists :/app.png" << QFile::exists(":/app.png");
    }

    void iconResourceDecodes()   // 主断言：app.png 在 qrc 且 QPixmap 能解码
    {
        QPixmap pm(QStringLiteral(":/app.png"));
        QVERIFY2(!pm.isNull(), "app.png 资源缺失或无法解码");
        QCOMPARE(pm.width(), 256);
        QCOMPARE(pm.height(), 256);
    }

    void iconYieldsPixmap()      // QIcon 出图（title bar 实际消费的形态）
    {
        QIcon icon(QStringLiteral(":/app.png"));
        QVERIFY2(!icon.pixmap(16, 16).isNull(),
                 "QIcon 从 app.png 取 16px pixmap 为空");
    }

    void themeResourceLoads()    // 对照组：同一 qrc 的 theme.qss 正常
    {
        QFile f(QStringLiteral(":/theme.qss"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().size() > 100);
    }
};

QTEST_MAIN(TestIcon)
#include "tst_icon.moc"
