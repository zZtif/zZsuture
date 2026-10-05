// tst_epnum.cpp —— P6/P8：文件名集数识别（模式按六组真实文件名校准）
#include <QtTest>

#include "epnum.hpp"
#include "load.hpp"

using namespace suture;

class TestEpNum : public QObject {
    Q_OBJECT
private slots:
    void patterns_data();
    void patterns();
    void naturalSort();
    void dropExpansion();
};

void TestEpNum::patterns_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<int>("expected");
    QTest::addColumn<bool>("confident");

    // ---- 六组真实文件名 ----
    QTest::newRow("G1") << "[VCB-Studio] Darker Than Black-Kuro no Keiyakusha [01][Hi10p_1080p][x264_flac].sc.txt" << 1 << true;
    QTest::newRow("G2") << "[Kpdm&X2][Shigofumi][BDrip][02][x264_AACx2][CRC32_C928331E].uni_big5.txt" << 2 << true;
    QTest::newRow("G3") << "[Nekomoe kissaten&LoliHouse] Gimai Seikatsu - 03 [WebRip 1080p HEVC-10bit AAC ASSx2].JPSC.txt" << 3 << true;
    QTest::newRow("G4") << "[Nekomoe kissaten] Make Heroine ga Oosugiru! [12][WebRip].JPSC.txt" << 12 << true;
    QTest::newRow("G5特别篇") << "[VCB-Studio] CHAOS;CHILD [00][Ma10p_1080p][x265_flac].txt" << 0 << true;
    QTest::newRow("G6全角括号") << "【S01】【04】【装铠龙的愿望】【HEVC 3840×2160 FLAC】.txt" << 4 << true;
    // ---- 干扰项排除 ----
    QTest::newRow("季号不算集") << "【S02】标题.txt" << -1 << false;
    QTest::newRow("分辨率不算集") << "某字幕 1080p 3840×2160.txt" << -1 << false;
    QTest::newRow("x264不算") << "[Group] Title [x264_flac].txt" << -1 << false;
    // ---- 边界 ----
    QTest::newRow("三位集数") << "[Group] Title [100][END].txt" << 100 << true;
}

void TestEpNum::patterns()
{
    QFETCH(QString, fileName);
    QFETCH(int, expected);
    QFETCH(bool, confident);
    const EpGuess g = guessEpisodeNumber(fileName);
    QCOMPARE(g.number, expected);
    QCOMPARE(g.confident, confident);
}

void TestEpNum::naturalSort()
{
    // 识别失败兜底：自然排序 "2" < "10"（字典序则相反）
    QVERIFY(naturalLessThan(QStringLiteral("第2集.ass"), QStringLiteral("第10集.ass")));
    QVERIFY(!naturalLessThan(QStringLiteral("第10集.ass"), QStringLiteral("第2集.ass")));
}

void TestEpNum::dropExpansion()
{
    // expandDrop：文件夹展开/非字幕过滤/去重（用 QTemporaryDir 构造）
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString base = dir.path();
    QFile f1(base + "/[01].ass");
    QFile f2(base + "/[02].txt");
    QFile f3(base + "/video.mkv");
    QFile f4(base + "/note.md");
    for (QFile* f : {&f1, &f2, &f3, &f4}) {
        QVERIFY(f->open(QIODevice::WriteOnly));
        f->write("x");
        f->close();
    }
    const DropExpansion e = expandDrop({QUrl::fromLocalFile(base)});
    QCOMPARE(int(e.files.size()), 2);   // mkv 与 md 被忽略
    bool hasIgnored = false;
    for (const auto& d : e.diags)
        if (d.code == DiagCode::NonSubtitleIgnored)
            hasIgnored = true;
    QVERIFY(hasIgnored);

    // 去重：同一文件夹拖两次
    const DropExpansion e2 = expandDrop({QUrl::fromLocalFile(base),
                                         QUrl::fromLocalFile(base)});
    QCOMPARE(int(e2.files.size()), 2);
    bool hasDup = false;
    for (const auto& d : e2.diags)
        if (d.code == DiagCode::DuplicateFile)
            hasDup = true;
    QVERIFY(hasDup);
}

QTEST_MAIN(TestEpNum)
#include "tst_epnum.moc"
