// tst_roundtrip.cpp —— 阶段 1 验收：六组 24 文件全量"装载→解析→序列化→再解析"
// 语义等价验证（S6）。样本目录经编译宏 SUTURE_TEST_DATA_DIR 传入（仓库外 ../测试）。
// 同时把往返输出写到构建目录 roundtrip/ 下，供人工用 Aegisub 打开复验。
#include <QDir>
#include <QFile>
#include <QtTest>

#include "assparse.hpp"
#include "asswrite.hpp"
#include "load.hpp"
#include "textdec.hpp"

using namespace suture;

class TestRoundTrip : public QObject {
    Q_OBJECT
private slots:
    void allSamples_data();
    void allSamples();
};

void TestRoundTrip::allSamples_data()
{
    QTest::addColumn<QString>("path");

    const QString dataDir = QStringLiteral(SUTURE_TEST_DATA_DIR);
    if (!QDir(dataDir).exists())
        QSKIP("测试数据目录不存在（本机开发环境应有 ../测试）");

    for (int g = 1; g <= 6; ++g) {
        const QDir group(dataDir + QStringLiteral("/%1").arg(g));
        const auto files = group.entryInfoList(
            {QStringLiteral("*.txt"), QStringLiteral("*.ass"),
             QStringLiteral("*.ssa")},
            QDir::Files, QDir::Name);
        for (const QFileInfo& f : files)
            QTest::newRow(QStringLiteral("G%1 %2").arg(g).arg(f.fileName())
                              .toUtf8())
                << f.absoluteFilePath();
    }
}

void TestRoundTrip::allSamples()
{
    QFETCH(QString, path);

    // 1) 装载（编码探测 + 格式判定 + 解析全链路）
    const LoadOutcome l1 = loadSubtitleFile(path, 1);
    QVERIFY2(l1.ok, qPrintable(path));
    const AssDocument d1 = l1.sub->doc;
    QVERIFY2(!d1.events.empty(),
             qPrintable(QStringLiteral("无事件行: %1").arg(path)));

    // 2) 序列化（UTF-8 BOM + CRLF）
    const QByteArray bytes = serializeAss(d1);
    QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));

    // 3) 输出落盘，供人工 Aegisub 复验（S6 验收项）
    QDir outDir(QCoreApplication::applicationDirPath() +
                QStringLiteral("/roundtrip"));
    outDir.mkpath(QStringLiteral("."));
    const QString outName =
        QStringLiteral("G%1_%2").arg(path.section(u'/', -2, -2))
        .arg(QFileInfo(path).completeBaseName().left(40)) +
        QStringLiteral(".ass");
    QFile out(outDir.filePath(outName));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(bytes);
    out.close();

    // 4) 再解析（输出规范为 UTF-8 BOM，直接走解析器）
    const ParseOutcome p2 = parseAss(QString::fromUtf8(bytes.mid(3)));
    QVERIFY2(p2.ok, "往返输出不可解析");
    const AssDocument& d2 = p2.doc;

    // 5) 语义等价：事件数/逐行时间/样式/文本/派生数据
    QCOMPARE(int(d2.events.size()), int(d1.events.size()));
    for (size_t i = 0; i < d1.events.size(); ++i) {
        const EventLine& a = d1.events[i];
        const EventLine& b = d2.events[i];
        QVERIFY2(a.start == b.start,
                 qPrintable(QStringLiteral("行%1 start 不一致").arg(i)));
        QVERIFY2(a.end == b.end, qPrintable(QStringLiteral("行%1 end 不一致").arg(i)));
        QVERIFY2(a.layer == b.layer,
                 qPrintable(QStringLiteral("行%1 layer 不一致").arg(i)));
        QVERIFY2(a.style == b.style,
                 qPrintable(QStringLiteral("行%1 style 不一致: %2 vs %3")
                                .arg(i)
                                .arg(a.style, b.style)));
        QVERIFY2(a.text == b.text,
                 qPrintable(QStringLiteral("行%1 text 不一致: %2 vs %3")
                                .arg(i)
                                .arg(a.text.left(30), b.text.left(30))));
        QVERIFY2(a.isComment == b.isComment,
                 qPrintable(QStringLiteral("行%1 Comment 标志不一致").arg(i)));
    }
    QCOMPARE(int(d2.styles.size()), int(d1.styles.size()));
    for (size_t i = 0; i < d1.styles.size(); ++i)
        QVERIFY2(d1.styles[i].originalName == d2.styles[i].originalName,
                 qPrintable(QStringLiteral("样式%1 名字不一致").arg(i)));
    // 派生数据一致（T2：maxEnd 在乱序文件上也必须一致）
    QCOMPARE(d2.maxDialogueEnd, d1.maxDialogueEnd);
    // Script Info 关键字段不丢
    QCOMPARE(d2.info.playRes.has_value(), d1.info.playRes.has_value());
}

QTEST_MAIN(TestRoundTrip)
#include "tst_roundtrip.moc"
