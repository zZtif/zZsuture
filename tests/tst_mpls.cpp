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
    void chapterDtB();          // 链式：黑契 36 章 / 6 集（实测分界点全对）
    void chapterShigofumi();    // 链式：死后文 DISC2 30 章 / 5 集（实测全对）
    void chapterNoNearbyMark(); // 链式：60 秒内无章节 → 保持预估 + 告警
    void chapterInapplicable(); // 多正片项 → 不适用，回落 PlayItem
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
    // 黑契 D1 实测章节（36 个）。链式分界点：各集字幕最晚结束约 24:09.40
    // （VCB 帧级精确，集间无缝）→ 每步预估都落在真分界点几秒内 → 吸附命中。
    // 期望 = 所有者 PowerDVD 目测验证过的六个偏移。
    const QList<Cs> rels = {
        0, 31031, 40040, 74374, 132933, 141942, 144945, 153954, 162963,
        218018, 277878, 286887, 289890, 298598, 307607, 350350, 422822,
        431831, 434834, 444944, 453953, 497897, 567767, 576776, 579779,
        621321, 630330, 647947, 712712, 721721, 724724, 733433, 742442,
        792091, 857657, 866666};
    const MplsInfo info = makeChapterInfo(rels, Cs(870470));
    std::vector<Diagnostic> diags;
    const QList<Cs> lastEnds = {Cs(144940), Cs(144940), Cs(144940),
                                Cs(144940), Cs(144940), Cs(144940)};
    const ChapterMapping m = suggestChapterStarts(info, 6, lastEnds, diags);
    QVERIFY(m.ok);
    QCOMPARE(m.clip, QStringLiteral("00001"));
    QCOMPARE(m.offsets.size(), 6);
    const qint64 want[] = {0, 144945, 289890, 434834, 579779, 724724};
    for (int k = 0; k < 6; ++k)
        QCOMPARE(qint64(m.offsets[k]), want[k]);
    // 末集集界 = 项时长；其余 = 下一集分界点
    QCOMPARE(qint64(m.episodeEnds[5]), qint64(870470));
    QCOMPARE(qint64(m.episodeEnds[0]), qint64(144945));
    // 依据文本：链式预估 → 吸附章节
    QVERIFY(m.epBasis[1].contains(QStringLiteral("吸附章节 0:24:09.45")));
    QVERIFY(m.epBasis[0].contains(QStringLiteral("文件头")));
    QVERIFY(diags.empty());   // 全部吸附成功，无告警
}

void TestMpls::chapterShigofumi()
{
    // 死后文 DISC2 实测章节（30 个）+ 6~10 集字幕真实最晚结束（2014 年
    // ep6 含下集预告 23:41.78；2008 年 ep7-10 为 ED 歌词区 23:38~23:40）。
    // 链式逐步预估 → 吸附，四个分界点全部命中（所有者手动实测对轴无偏差）。
    const QList<Cs> rels = {
        0, 6907, 15916, 72623, 131823, 140824, 142526, 145529, 154538,
        216249, 274149, 283150, 284851, 290357, 299366, 354971, 416474,
        425475, 427177, 432782, 441791, 499999, 558800, 567601, 569502,
        574207, 583016, 632532, 701130, 710143};
    const MplsInfo info = makeChapterInfo(rels, Cs(713146));
    std::vector<Diagnostic> diags;
    const QList<Cs> lastEnds = {Cs(142178), Cs(141839), Cs(141992),
                                Cs(141923), Cs(142002)};
    const ChapterMapping m = suggestChapterStarts(info, 5, lastEnds, diags);
    QVERIFY(m.ok);
    const qint64 want[] = {0, 142526, 284851, 427177, 569502};
    for (int k = 0; k < 5; ++k)
        QCOMPARE(qint64(m.offsets[k]), want[k]);
    // ED 结束章节（如 1:10:54.75）距预估位仅 4~15 秒但不是最近——
    // 链式就近规则天然避开（这是旧 ED 锚定误判为"翻车"的区域）
    QVERIFY(m.epBasis[3].contains(QStringLiteral("吸附章节 1:11:11.77")));
    QVERIFY(diags.empty());
}

void TestMpls::chapterNoNearbyMark()
{
    // 链式预估附近 60 秒内没有章节（如无 ED 歌词的字幕提前结束）→
    // 保持预估值 + 告警人工核对（宁可明白地不准，不给可疑吸附值）
    const QList<Cs> rels = {0, 60000, 69009};   // 唯一远处的 90 秒对
    const MplsInfo info = makeChapterInfo(rels, 3 * 24 * 6000);
    std::vector<Diagnostic> diags;
    const ChapterMapping m = suggestChapterStarts(
        info, 2, {Cs(120000)}, diags);   // ep1 最后一句在 20:00（无 ED 歌词）
    QVERIFY(m.ok);
    QCOMPARE(qint64(m.offsets[0]), qint64(0));
    QCOMPARE(qint64(m.offsets[1]), qint64(120000));   // 预估值保留
    bool hasWarn = false;
    for (const auto& d : diags)
        if (d.level == DiagLevel::Warning && d.fileIndex == 1)
            hasWarn = true;
    QVERIFY(hasWarn);
    QVERIFY(m.epBasis[1].contains(QStringLiteral("请人工核对")));
}

void TestMpls::chapterInapplicable()
{
    // 多个正片项（一集一项的规整盘型）→ 章节模式不适用，调用方走 PlayItem 映射
    {
        const MplsInfo info = makeChapterInfo({0, 100000}, 24 * 6000, 1);
        std::vector<Diagnostic> diags;
        QVERIFY(!suggestChapterStarts(info, 2, {}, diags).ok);
    }
    {   // 零集
        std::vector<Diagnostic> diags;
        QVERIFY(!suggestChapterStarts(makeChapterInfo({0}, 24 * 6000), 0,
                                      {}, diags).ok);
    }
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
