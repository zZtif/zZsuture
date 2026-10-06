// tst_drag.cpp —— F02：文件列表鼠标拖拽排序（自动化——用 QTest 模拟
// 按下/移动/松开，之前三次人工验证拖拽失效的教训：不再盲发，机器先验）
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include "filelistmodel.hpp"
#include "session.hpp"

using namespace suture;

namespace {
// 最小合法 ASS（能通过完整装载链路）
const char* kAss =
    "[Script Info]\nScriptType: v4.00+\n\n"
    "[V4+ Styles]\nFormat: Name, Fontname, Fontsize\nStyle: Default,Arial,20\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Actor, MarginL, MarginR, "
    "MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:23:00.00,Default,,0,0,0,,x\n";
}  // namespace

class TestDrag : public QObject {
    Q_OBJECT
private slots:
    void reorderTop();     // 第 4 行拖到第 1 行
    void reorderBottom();  // 第 1 行拖到末尾
    void samePlace();      // 原地松开 = 无操作
};

namespace {
struct Fixture {
    QTemporaryDir dir;
    Session session;
    FileListModel model{&session};
    FileTableView view;

    Fixture()
    {
        for (int i = 1; i <= 4; ++i) {
            const QString p = dir.filePath(
                QStringLiteral("[%1].ass").arg(i, 2, 10, QChar(u'0')));
            QFile f(p);
            f.open(QIODevice::WriteOnly);
            f.write(kAss);
            f.close();
        }
        QList<QUrl> urls;
        const QStringList files = QDir(dir.path()).entryList(
            {QStringLiteral("*.ass")}, QDir::Files);
        for (const QString& n : files)
            urls << QUrl::fromLocalFile(dir.filePath(n));
        session.addUrls(urls);
        view.setModel(&model);
        // 真实程序里由 MainWindow 完成的接线（测试必须同样接上）
        QObject::connect(&view, &FileTableView::rowsDropped,
                         &session, &Session::moveRows);
        view.resize(900, 400);
        view.show();
    }

    // 模拟真实拖拽：按下 → 带左键状态的移动（QTest::mouseMove 不带按键
    // 状态，必须手动构造事件）→ 松开
    void dragFromTo(int fromRow, const QPoint& to)
    {
        auto* vp = view.viewport();
        const QPoint from = view.visualRect(view.model()->index(fromRow, 0)).center();
        QTest::mousePress(vp, Qt::LeftButton, Qt::NoModifier, from);
        const QPoint mid = from + (to - from) / 2 + QPoint(40, 0);
        for (const QPoint& p : {mid, to}) {
            QMouseEvent mv(QEvent::MouseMove, p, vp->mapToGlobal(p),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &mv);
        }
        QTest::mouseRelease(vp, Qt::LeftButton, Qt::NoModifier, to);
    }
};
}  // namespace

void TestDrag::reorderTop()
{
    Fixture fx;
    QCOMPARE(fx.session.count(), 4);
    QCOMPARE(QFileInfo(fx.session.entry(3).sub->path).fileName(),
             QStringLiteral("[04].ass"));

    // 第 4 行拖到第 1 行上半部
    fx.dragFromTo(3, fx.view.visualRect(fx.view.model()->index(0, 0)).center()
                         - QPoint(0, 6));

    QCOMPARE(QFileInfo(fx.session.entry(0).sub->path).fileName(),
             QStringLiteral("[04].ass"));
    QCOMPARE(QFileInfo(fx.session.entry(1).sub->path).fileName(),
             QStringLiteral("[01].ass"));
    // 预填连锁重算：新的第 1 行偏移归零
    QCOMPARE(fx.session.entry(0).offset, Cs(0));
}

void TestDrag::reorderBottom()
{
    Fixture fx;
    fx.dragFromTo(0, fx.view.visualRect(fx.view.model()->index(3, 0)).center()
                        + QPoint(0, 6));

    QCOMPARE(QFileInfo(fx.session.entry(3).sub->path).fileName(),
             QStringLiteral("[01].ass"));
    QCOMPARE(QFileInfo(fx.session.entry(0).sub->path).fileName(),
             QStringLiteral("[02].ass"));
}

void TestDrag::samePlace()
{
    Fixture fx;
    fx.dragFromTo(1, fx.view.visualRect(fx.view.model()->index(1, 0)).center()
                        + QPoint(40, 20));   // 移动但仍在原行

    // 原地松开：顺序不变
    QCOMPARE(QFileInfo(fx.session.entry(0).sub->path).fileName(),
             QStringLiteral("[01].ass"));
    QCOMPARE(QFileInfo(fx.session.entry(1).sub->path).fileName(),
             QStringLiteral("[02].ass"));
}

QTEST_MAIN(TestDrag)
#include "tst_drag.moc"
