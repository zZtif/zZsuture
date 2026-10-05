// tst_mpls.cpp —— T1/T3/D10：MPLS 二进制解析 + PlayItem↔集数映射
// 布局按真实原盘（GTO US 00003.mpls）逆向实证修正：PlayItem 项长为 u16，
// IN 在项内 +14、OUT 在 +18。SUTURE_TEST_MPLS 环境变量指向真实文件时可跑
// 实盘回归（本机为 GTO 44 集链，按文件大小识别并断言已知结果）。
#include <QFile>
#include <QtTest>

#include "mpls.hpp"

using namespace suture;

namespace {
QByteArray be32(quint32 v)
{
    QByteArray b(4, 0);
    b[0] = char(v >> 24); b[1] = char(v >> 16); b[2] = char(v >> 8); b[3] = char(v);
    return b;
}
QByteArray be16(quint16 v)
{
    QByteArray b(2, 0);
    b[0] = char(v >> 8); b[1] = char(v);
    return b;
}

struct ItemSpec {
    quint32 in45k, out45k;   // 45kHz tick
    int     extraBytes = 0;  // still 之后的附加数据（模拟 STN 表/多角度）
};

// 按实证布局合成 MPLS（PlayList 段在 0x40）
// PlayItem：u16 项长 + clip[5] + "M2TS"[4] + 3B 连接 + IN[u32] + OUT[u32]
//           + UO 表[8] + still[3] + 附加数据
QByteArray buildMpls(const QList<ItemSpec>& items, const char* version = "0101")
{
    QByteArray h = QByteArray("MPLS", 4) + QByteArray(version, 4);
    h += be32(0x40);   // playlist_start_address
    h += be32(0);      // playListMark_start_address
    h += be32(0);      // extension_start_address
    h += QByteArray(0x40 - h.size(), 0);

    QByteArray itemsData;
    for (const ItemSpec& it : items) {
        QByteArray body;
        body += QByteArray("00000", 5);      // clip file id
        body += QByteArray("M2TS", 4);       // codec id
        body += QByteArray(3, 0);            // 连接信息
        body += be32(it.in45k);
        body += be32(it.out45k);
        body += QByteArray(8, 0);            // UO mask
        body += char(0);                     // still mode
        body += be16(0);                     // still time
        body += QByteArray(it.extraBytes, 0xAB);   // STN/多角度等附加
        itemsData += be16(quint16(body.size())) + body;
    }
    QByteArray pl;
    // 段长语义：u32 计长度字段之后的字节数（6 计数 + items）
    pl += be32(quint32(6 + itemsData.size()));
    pl += be16(0);                                 // 保留
    pl += be16(quint16(items.size()));             // PlayItem 数
    pl += be16(0);                                 // SubPath 数
    pl += itemsData;
    return h + pl;
}

// 厘秒 → 45kHz tick（1 cs = 450 tick）
constexpr quint32 ticks(Cs cs) { return quint32(cs * 450); }
}  // namespace

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
    const QByteArray raw = buildMpls({{0, ticks(6000)}}, "0300");
    const MplsOutcome out = parseMpls(raw);
    QVERIFY(out.info.has_value());    // 未知版本：仍解析 + Info 告知
    bool hasInfo = false;
    for (const auto& d : out.diags)
        if (d.code == DiagCode::MplsUnsupported && d.level == DiagLevel::Info)
            hasInfo = true;
    QVERIFY(hasInfo);
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
    }
}

QTEST_MAIN(TestMpls)
#include "tst_mpls.moc"
