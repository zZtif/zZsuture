// tst_mpls.cpp —— T1/T3/D10：MPLS 二进制解析 + PlayItem↔集数映射
// 布局按真实原盘（GTO US 00003.mpls）逆向实证修正：PlayItem 项长为 u16，
// IN 在项内 +14、OUT 在 +18。SUTURE_TEST_MPLS 环境变量指向真实文件时可跑
// 实盘回归（本机为 GTO 44 集链，按文件大小识别并断言已知结果）。
#include <QFile>
#include <QtTest>

#include "mpls.hpp"
#include "mplsfixture.hpp"

using namespace suture;
using namespace suture::testfix;

class TestMpls : public QObject {
    Q_OBJECT
private slots:
    void validWithLogo();       // Logo + 两集正片 → 偏移含 Logo
    void startMainMapping();    // 整季链只合并中间几集（起始正片）
    void notEnoughMains();      // 起始项+集数超出正片总数 → 告警不猜
    void multiAngleSkip();      // 附加数据靠 u16 项长跳过
    void badMagic();
    void truncated();
    void inAfterOut();
    void unknownVersion();      // 未知版本 → Info + 仍解析
    void marksAndClips();       // T8：章节 + clip 名解析
    void marksDegrade();        // T8：章节段截断 → 告警降级，PlayItem 保留
    void chapterDtB();          // T8：黑契型 36 章 / 6 集，ED 锚定
    void chapterShigofumi();    // T8：死后文型 29 章，ED 锚定（实测 bug 回归）
    void chapterEdAnchorBeatsDp();  // T8 修正：ED 锚定纠正基线 DP 的吸附错误
    void chapterInapplicable(); // T8：多正片项 / 无章节 → 不适用，回落 PlayItem
    void chapterEquidivide();   // T8：候选不足 → 等分预填 + 告警
    void chapterSnapFail();     // T8：吸附距离超容差 → 回落等分 + 告警
    void degeneratePlaylist();  // T9：全同连续项 → 疑似假列表告警（义妹型）
    void realDiscFile();        // 真实原盘回归（需环境变量，否则跳过）
};

void TestMpls::validWithLogo()
{
    // 30 秒 Logo + 24 分钟正片 + 25 分钟正片
    const QByteArray raw = buildMpls({
        {0, ticks(30 * 100)},                              // Logo
        {ticks(30 * 100), ticks(30 * 100 + 24 * 6000)},    // EP1
        {0, ticks(25 * 6000)},                             // EP2
    });
    const MplsOutcome out = parseMpls(raw);
    QVERIFY2(out.info.has_value(),
             out.diags.empty() ? QByteArray("无诊断")
                               : out.diags.front().message.toUtf8());
    QCOMPARE(int(out.info->items.size()), 3);
    QCOMPARE(out.info->totalCs, Cs(30 * 100 + 24 * 6000 + 25 * 6000));

    std::vector<Diagnostic> diags;
    const MplsMapping m = mapItemsToEpisodes(*out.info, 2, 0, diags);
    QVERIFY(m.ok);
    QVERIFY(diags.empty());
    QCOMPARE(int(m.mainItems.size()), 2);   // Logo 不算正片
    // EP1 起点 = Logo 之后；EP2 起点 = Logo + EP1
    QVERIFY(qAbs(m.offsets[0] - Cs(3000)) <= 1);
    QVERIFY(qAbs(m.offsets[1] - Cs(3000 + 24 * 6000)) <= 1);
    QVERIFY(qAbs(m.episodeEnds[0] - Cs(3000 + 24 * 6000)) <= 1);
}

void TestMpls::startMainMapping()
{
    // 整季链：Logo + 3 个正片；用户只合并后 2 集（字幕第 1 集 = 第 2 个正片）
    const QByteArray raw = buildMpls({
        {0, ticks(20 * 100)},
        {0, ticks(24 * 6000)},
        {0, ticks(25 * 6000)},
        {0, ticks(26 * 6000)},
    });
    const MplsOutcome out = parseMpls(raw);
    QVERIFY(out.info.has_value());
    std::vector<Diagnostic> diags;
    const MplsMapping m = mapItemsToEpisodes(*out.info, 2, 1, diags);
    QVERIFY(m.ok);
    QCOMPARE(int(m.mainItems.size()), 3);
    // 字幕第 1 集起点 = Logo + 第 1 个正片（被跳过的内容也计入时间轴）
    QVERIFY(qAbs(m.offsets[0] - Cs(2000 + 24 * 6000)) <= 1);
    QVERIFY(qAbs(m.offsets[1] - Cs(2000 + 24 * 6000 + 25 * 6000)) <= 1);
    QVERIFY(qAbs(m.episodeEnds[1] - Cs(2000 + 24 * 6000 + 25 * 6000 + 26 * 6000)) <= 1);
    // 有 Info 提示起始偏移含前序内容
    bool hasInfo = false;
    for (const auto& d : diags)
        if (d.level == DiagLevel::Info)
            hasInfo = true;
    QVERIFY(hasInfo);
}

void TestMpls::notEnoughMains()
{
    const QByteArray raw = buildMpls({
        {0, ticks(24 * 6000)},
        {0, ticks(24 * 6000)},
    });
    const MplsOutcome out = parseMpls(raw);
    QVERIFY(out.info.has_value());
    std::vector<Diagnostic> diags;
    const MplsMapping m = mapItemsToEpisodes(*out.info, 3, 0, diags);   // 3 集但只有 2 正片
    QVERIFY(!m.ok);
    QVERIFY(m.offsets.isEmpty());
    QVERIFY(!diags.empty());
    QVERIFY(diags[0].code == DiagCode::MplsUnsupported);
}

void TestMpls::multiAngleSkip()
{
    // 第 1 项带 40 字节附加数据，第 2 项仍应被正确解析
    const QByteArray raw = buildMpls({
        {0, ticks(24 * 6000), 40},
        {0, ticks(24 * 6000)},
    });
    const MplsOutcome out = parseMpls(raw);
    QVERIFY2(out.info.has_value(),
             out.diags.empty() ? QByteArray("无诊断")
                               : out.diags.front().message.toUtf8());
    QCOMPARE(int(out.info->items.size()), 2);
    QVERIFY(qAbs(out.info->items[1].durationCs - Cs(24 * 6000)) <= 1);
}

void TestMpls::badMagic()
{
    QByteArray raw = buildMpls({{0, ticks(6000)}});
    raw[0] = 'X';
    const MplsOutcome out = parseMpls(raw);
    QVERIFY(!out.info.has_value());
    QVERIFY(!out.diags.empty());
}

void TestMpls::truncated()
{
    const QByteArray full = buildMpls({{0, ticks(6000)}, {0, ticks(6000)}});
    const MplsOutcome out = parseMpls(full.left(full.size() - 10));
    QVERIFY(!out.info.has_value());   // 越界防御生效，不崩溃
}

void TestMpls::inAfterOut()
{
    const MplsOutcome out = parseMpls(buildMpls({{ticks(6000), ticks(3000)}}));
    QVERIFY(!out.info.has_value());   // OUT < IN → 拒绝
}

void TestMpls::unknownVersion()
{
    const QByteArray raw = buildMpls({{0, ticks(6000)}}, {}, "0300");
    const MplsOutcome out = parseMpls(raw);
    QVERIFY(out.info.has_value());    // 未知版本：仍解析 + Info 告知
    bool hasInfo = false;
    for (const auto& d : out.diags)
        if (d.code == DiagCode::MplsUnsupported && d.level == DiagLevel::Info)
            hasInfo = true;
    QVERIFY(hasInfo);
}

void TestMpls::marksAndClips()
{
    // T8：单正片项（多集一个 m2ts）+ 章节标记
    const quint32 in = ticks(600 * 100);   // 项内时间轴原点 10:00.00
    const QByteArray raw = buildMpls(
        {{in, in + ticks(5 * 24 * 6000), 0, "00001"}},
        {{1, 0, in},                       // 集起点 = IN（rel 0）
         {1, 0, in + ticks(90 * 100 + 9)}, // OP 结束（+90.09s，分集点签名）
         {2, 0, in + ticks(24 * 6000 + 9 * 100 + 45)},   // 非章节类型（type=2）
         {1, 1, in}});                     // 引用不存在的项 1 → 忽略 + 告警
    const MplsOutcome out = parseMpls(raw);
    QVERIFY2(out.info.has_value(),
             out.diags.empty() ? QByteArray("无诊断")
                               : out.diags.front().message.toUtf8());
    QCOMPARE(out.info->items[0].clip, QStringLiteral("00001"));
    QCOMPARE(int(out.info->marks.size()), 3);   // 第 4 条被忽略
    QCOMPARE(out.info->marks[0].type, quint16(1));
    QCOMPARE(out.info->marks[0].itemIndex, 0);
    QCOMPARE(qint64(out.info->marks[0].time45k), qint64(in));
    QCOMPARE(out.info->marks[2].type, quint16(2));   // 非 Entry Mark 原样保留
    bool hasSkip = false;
    for (const auto& d : out.diags)
        if (d.level == DiagLevel::Warning && d.code == DiagCode::MplsUnsupported)
            hasSkip = true;
    QVERIFY(hasSkip);
}

void TestMpls::marksDegrade()
{
    // 章节段被截断：告警降级为无章节，PlayItem 解析结果保留（宁可没有章节）
    QByteArray raw = buildMpls(
        {{0, ticks(24 * 6000)}},
        {{1, 0, 0}, {1, 0, ticks(6000)}, {1, 0, ticks(12000)}});
    raw.truncate(raw.size() - 10);   // 咬掉最后一条章节的一部分
    const MplsOutcome out = parseMpls(raw);
    QVERIFY(out.info.has_value());
    QCOMPARE(int(out.info->items.size()), 1);
    QVERIFY(out.info->marks.isEmpty());
    bool hasWarn = false;
    for (const auto& d : out.diags)
        if (d.level == DiagLevel::Warning && d.code == DiagCode::MplsUnsupported)
            hasWarn = true;
    QVERIFY(hasWarn);
}

void TestMpls::chapterDtB()
{
    // 黑契 D1 实测章节（36 个）+ 六集字幕真实 ED 起点（各集 22:09.35）。
    // ED 锚定期望 = 盘 ED 章节 − 字幕 ED：与所有者 PowerDVD 目测验证过的
    // 偏移（0 / 24:09.45 / 48:18.90 / 1:12:28.34 / 1:36:37.79 / 2:00:47.24）
    // 吻合到 0.03 秒内。
    const QList<Cs> rels = {
        0, 31031, 40040, 74374, 132933, 141942, 144945, 153954, 162963,
        218018, 277878, 286887, 289890, 298598, 307607, 350350, 422822,
        431831, 434834, 444944, 453953, 497897, 567767, 576776, 579779,
        621321, 630330, 647947, 712712, 721721, 724724, 733433, 742442,
        792091, 857657, 866666};
    const MplsInfo info = makeChapterInfo(rels, Cs(870470));
    std::vector<Diagnostic> diags;
    const ChapterMapping m = suggestChapterStarts(
        info, 6, {Cs(132935), Cs(132935), Cs(132935), Cs(132935), Cs(132935),
                  Cs(132935)}, diags);
    QVERIFY(m.ok);
    QCOMPARE(m.clip, QStringLiteral("00001"));
    QCOMPARE(m.offsets.size(), 6);
    const qint64 want[] = {0, 144943, 289887, 434832, 579777, 724722};
    for (int k = 0; k < 6; ++k)
        QCOMPARE(qint64(m.offsets[k]), want[k]);
    // 末集集界 = 项时长；其余 = 下一集起点
    QCOMPARE(qint64(m.episodeEnds[5]), qint64(870470));
    QCOMPARE(qint64(m.episodeEnds[0]), qint64(144943));
    // A-r2：ED 锚定标记与依据（解析页纯数轴的 ▽ 标记与悬停文本来源）
    QCOMPARE(m.edAnchorMarks.size(), 6);
    QCOMPARE(qint64(m.edAnchorMarks[0]), qint64(-1));      // 第 1 集不锚定
    QCOMPARE(qint64(m.edAnchorMarks[1]), qint64(277878));  // 46:18.78
    QCOMPARE(qint64(m.edAnchorMarks[5]), qint64(857657));  // 2:22:56.57
    QVERIFY(m.epBasis[0].isEmpty());
    QVERIFY(m.epBasis[1].contains(QStringLiteral("0:46:18.78")));
}

void TestMpls::chapterShigofumi()
{
    // 死后文 D1 实测章节（29 个，每集结构 [预告][首语][OP 90.09s][A/B]
    // [ED 90.09s]——分集起点"首语"不带签名，纯签名启发会吸到 OP 起点偏晚
    // 27.6 秒，即 2026-10-08 所有者实测报告的 bug）。
    // 字幕真实 ED 起点：ep1 22:04.66（晚于歌曲起点 6.5s，老字幕组习惯，
    // 故第 1 集不锚定保持 0）；ep2-5 22:02.57/22:02.67。
    const QList<Cs> rels = {
        0, 7608, 60711, 131815, 140824, 142526, 144928, 153937, 214047,
        274157, 283166, 284868, 291975, 300984, 357490, 416491, 425492,
        427193, 434000, 443009, 496713, 558817, 567817, 569519, 575525,
        584534, 644043, 701142, 710143};
    const MplsInfo info = makeChapterInfo(rels, Cs(713146));
    std::vector<Diagnostic> diags;
    const ChapterMapping m = suggestChapterStarts(
        info, 5, {Cs(132466), Cs(132257), Cs(132257), Cs(132267), Cs(132267)},
        diags);
    QVERIFY(m.ok);
    // ep1 不锚定 = 0；ep2-5 按 ED 锚定（与 OP 起点锚定交叉验证差 ≤2.7 秒，
    // 为老字幕 TV 时轴与 BD 版的固有内部漂移，全局微调收敛）
    const qint64 want[] = {0, 141900, 284234, 426550, 568875};
    for (int k = 0; k < 5; ++k)
        QCOMPARE(qint64(m.offsets[k]), want[k]);
    // A-r2：ED 锚定标记（盘上的 ED 章节实时刻）
    const qint64 edWant[] = {-1, 274157, 416491, 558817, 701142};
    for (int k = 0; k < 5; ++k)
        QCOMPARE(qint64(m.edAnchorMarks[k]), edWant[k]);
}

void TestMpls::chapterEdAnchorBeatsDp()
{
    // 分集起点无章节、近处有诱饵签名章节（OP 起点）：基线 DP 吸到诱饵
    // （偏晚 30 秒），ED 锚定给出正确值——死后文 bug 的最小化复现。
    // 盘：2 集 × 24 分钟；ep2 起点 24:00 无标记；诱饵 OP 对 24:30→26:00.09；
    // ED 对 44:00→45:30.09；噪声 33:20。
    const QList<Cs> rels = {0, 147000, 156009, 200000, 264000, 273009};
    const MplsInfo info = makeChapterInfo(rels, 2 * 24 * 6000);
    std::vector<Diagnostic> diags;
    const ChapterMapping m = suggestChapterStarts(
        info, 2, {Cs(-1), Cs(20 * 6000)}, diags);   // ep2 字幕 ED 在其 20:00
    QVERIFY(m.ok);
    QCOMPARE(qint64(m.offsets[0]), qint64(0));            // 第 1 集不锚定
    QCOMPARE(qint64(m.offsets[1]), qint64(24 * 6000));    // 24:00，而非诱饵 24:30
    bool hasEdInfo = false;
    for (const auto& d : diags)
        if (d.code == DiagCode::MplsUnsupported && d.fileIndex == 1
            && d.message.contains(QStringLiteral("片尾曲")))
            hasEdInfo = true;
    QVERIFY(hasEdInfo);
}

void TestMpls::chapterInapplicable()
{
    // 多个正片项（一集一项的规整盘型）→ 章节模式不适用，调用方走 PlayItem 映射
    {
        const MplsInfo info = makeChapterInfo({0, 100000}, 24 * 6000, 1);
        std::vector<Diagnostic> diags;
        QVERIFY(!suggestChapterStarts(info, 2, {}, diags).ok);
    }
    {   // 无章节
        const MplsInfo info = makeChapterInfo({}, 24 * 6000);
        std::vector<Diagnostic> diags;
        QVERIFY(!suggestChapterStarts(info, 2, {}, diags).ok);
    }
}

void TestMpls::chapterEquidivide()
{
    // 候选数（2）< 集数（3）→ DP 不可行：等分预填 + 告警，人工核对
    const MplsInfo info = makeChapterInfo({0, 10000}, 3 * 24 * 6000);
    std::vector<Diagnostic> diags;
    const ChapterMapping m = suggestChapterStarts(info, 3, {}, diags);
    QVERIFY(m.ok);
    QCOMPARE(qint64(m.offsets[0]), qint64(0));
    QCOMPARE(qint64(m.offsets[1]), qint64(24 * 6000));
    QCOMPARE(qint64(m.offsets[2]), qint64(2 * 24 * 6000));
    bool hasWarn = false;
    for (const auto& d : diags)
        if (d.level == DiagLevel::Warning)
            hasWarn = true;
    QVERIFY(hasWarn);
}

void TestMpls::chapterSnapFail()
{
    // 唯一候选离等分位 250 秒（> 150 容差）→ 回落等分值 + 按集告警
    const MplsInfo info = makeChapterInfo({0, 85000}, 2 * 600 * 100);
    std::vector<Diagnostic> diags;
    const ChapterMapping m = suggestChapterStarts(info, 2, {}, diags);
    QVERIFY(m.ok);
    QCOMPARE(qint64(m.offsets[0]), qint64(0));
    QCOMPARE(qint64(m.offsets[1]), qint64(60000));   // 850 秒候选被拒，回落 600 秒
    QCOMPARE(diags.empty(), false);
    bool snapWarn = false;
    for (const auto& d : diags)
        if (d.level == DiagLevel::Warning && d.fileIndex == 1)
            snapWarn = true;
    QVERIFY(snapWarn);
}

void TestMpls::degeneratePlaylist()
{
    // 义妹 00000.mpls 实测型：50 个相同 2:47 项冒充"总时长最长"的假列表。
    // 结构合法 → 解析照常 + Warning 提示换文件。
    const QByteArray raw = buildMpls({
        {0, ticks(16700), 0, "00000"},
        {0, ticks(16700), 0, "00000"},
        {0, ticks(16700), 0, "00000"},
        {0, ticks(16700), 0, "00000"},
    });
    const MplsOutcome out = parseMpls(raw);
    QVERIFY(out.info.has_value());           // 解析不拒绝
    QCOMPARE(int(out.info->items.size()), 4);
    bool hasWarn = false;
    for (const auto& d : out.diags)
        if (d.level == DiagLevel::Warning && d.code == DiagCode::MplsUnsupported)
            hasWarn = true;
    QVERIFY(hasWarn);
}

void TestMpls::realDiscFile()
{
    // 真实原盘回归：设置 SUTURE_TEST_MPLS 指向 .mpls 文件（如 GTO US 00003.mpls）
    const QString path = qEnvironmentVariable("SUTURE_TEST_MPLS");
    if (path.isEmpty())
        QSKIP("未设置 SUTURE_TEST_MPLS，跳过实盘回归");
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const MplsOutcome out = parseMpls(f.readAll());
    QVERIFY2(out.info.has_value(),
             out.diags.empty() ? QByteArray("无诊断")
                               : out.diags.front().message.toUtf8());
    QVERIFY(out.info->items.size() >= 1);   // 单项播放列表（菜单/单集）也合法
    QVERIFY(out.info->totalCs >= 0);
    // 本机 GTO 全系列链（9604 字节的 00003.mpls）：44 项 / 总时长约 17:49:32
    if (f.size() == 9604) {
        QCOMPARE(int(out.info->items.size()), 44);
        QVERIFY(qAbs(out.info->totalCs - Cs(17 * 360000 + 49 * 6000 + 32 * 100)) < 100);
        // 第 1 项（实证）：IN=10:00.000，时长 48:26.04
        QVERIFY(qAbs(qint64(out.info->items[0].in45k) - 27000000LL) < 10);
        QVERIFY(qAbs(out.info->items[0].durationCs - Cs(48 * 6000 + 26 * 100)) < 10);
        // T8 章节回归（实证）：219 个全为 Entry Mark；第 1 个 = 项 0 的 IN 整
        QCOMPARE(int(out.info->marks.size()), 219);
        QCOMPARE(out.info->marks[0].type, quint16(1));
        QCOMPARE(out.info->marks[0].itemIndex, 0);
        QVERIFY(qAbs(qint64(out.info->marks[0].time45k) - 27000000LL) < 10);
        bool allType1 = true;
        for (const auto& m : out.info->marks)
            allType1 = allType1 && m.type == 1;
        QVERIFY(allType1);
    }
}

QTEST_MAIN(TestMpls)
#include "tst_mpls.moc"
