// tst_mpls.cpp —— T1/T3/D10：MPLS 二进制解析 + PlayItem↔集数映射
// 用合成字节流构造（无真实原盘样例时的可测方案；真实盘验证在人工环节）
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
    int     extraBytes = 0;  // OUT 之后的附加数据（模拟多角度等）
};

// 按已知布局合成一个 MPLS 文件（PlayList 段在 0x40）
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
        body += be16(0);                     // 连接位
        body += char(0);                     // is_multi_angle
        body += char(0);                     // ref_to_STC_id
        body += be32(it.in45k);
        body += be32(it.out45k);
        body += QByteArray(8, 0);            // UO mask
        body += char(0);                     // still mode
        body += be16(0);                     // still time
        body += QByteArray(it.extraBytes, 0xAB);   // 附加数据
        itemsData += be32(quint32(body.size())) + body;
    }
    QByteArray pl;
    // 段长语义：u32 计的是长度字段之后的字节数（6 计数 + items）
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
    void countMismatch();       // D10：数量不符 → 告警不猜
    void multiAngleSkip();      // 多角度附加数据靠项长跳过
    void badMagic();
    void truncated();
    void inAfterOut();
    void unknownVersion();      // 未知版本 → Info + 仍解析
};

void TestMpls::validWithLogo()
{
    // 30 秒 Logo + 24 分钟正片 + 25 分钟正片
    const QByteArray raw = buildMpls({
        {0, ticks(30 * 100)},                    // Logo
        {ticks(30 * 100), ticks(30 * 100 + 24 * 6000)},   // EP1
        {0, ticks(25 * 6000)},                   // EP2
    });
    const MplsOutcome out = parseMpls(raw);
    QVERIFY2(out.info.has_value(),
             out.diags.empty() ? QByteArray("无诊断")
                               : out.diags.front().message.toUtf8());
    QCOMPARE(int(out.info->items.size()), 3);
    QCOMPARE(out.info->totalCs, Cs(30 * 100 + 24 * 6000 + 25 * 6000));
    // ticks() 有整除损失，容差 ±1cs 检查单项
    QVERIFY(qAbs(out.info->items[0].durationCs - Cs(3000)) <= 1);

    std::vector<Diagnostic> diags;
    const MplsMapping m = mapItemsToEpisodes(*out.info, 2, diags);
    QVERIFY(m.ok);
    QVERIFY(diags.empty());
    // EP1 起点 = Logo 之后；EP2 起点 = Logo + EP1
    QVERIFY(qAbs(m.offsets[0] - Cs(3000)) <= 1);
    QVERIFY(qAbs(m.offsets[1] - Cs(3000 + 24 * 6000)) <= 1);
    QVERIFY(qAbs(m.episodeEnds[0] - Cs(3000 + 24 * 6000)) <= 1);
}

void TestMpls::countMismatch()
{
    const QByteArray raw = buildMpls({
        {0, ticks(24 * 6000)},
        {0, ticks(24 * 6000)},
        {0, ticks(24 * 6000)},
    });
    const MplsOutcome out = parseMpls(raw);
    QVERIFY(out.info.has_value());
    std::vector<Diagnostic> diags;
    const MplsMapping m = mapItemsToEpisodes(*out.info, 2, diags);   // 3 项 vs 2 集
    QVERIFY(!m.ok);
    QVERIFY(m.offsets.isEmpty());
    QVERIFY(!diags.empty());
    QVERIFY(diags[0].code == DiagCode::MplsUnsupported);
}

void TestMpls::multiAngleSkip()
{
    // 第 1 项带 40 字节附加数据（多角度），第 2 项仍应被正确解析
    const QByteArray raw = buildMpls({
        {0, ticks(24 * 6000), 40},
        {0, ticks(24 * 6000)},
    });
    const MplsOutcome out = parseMpls(raw);
    QVERIFY(out.info.has_value());
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

QTEST_MAIN(TestMpls)
#include "tst_mpls.moc"
