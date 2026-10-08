// mplsfixture.hpp —— 测试共用夹具：按实证布局合成 MPLS 二进制 / MplsInfo
// tst_mpls（core 解析/建议）与 tst_drag（Session 集成，需真实二进制文件）
// 共用；布局注释见 tst_mpls.cpp 文件头。
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QChar>

#include "mpls.hpp"

namespace suture::testfix {

inline QByteArray be32(quint32 v)
{
    QByteArray b(4, 0);
    b[0] = char(v >> 24); b[1] = char(v >> 16); b[2] = char(v >> 8); b[3] = char(v);
    return b;
}

inline QByteArray be16(quint16 v)
{
    QByteArray b(2, 0);
    b[0] = char(v >> 8); b[1] = char(v);
    return b;
}

// 厘秒 → 45kHz tick（1 cs = 450 tick）
inline constexpr quint32 ticks(Cs cs) { return quint32(cs * 450); }

struct ItemSpec {
    quint32 in45k, out45k;   // 45kHz tick
    int     extraBytes = 0;  // still 之后的附加数据（模拟 STN 表/多角度）
    const char* clip = "00000";   // clip 文件名（F07 同名导出）
};

struct MarkSpec {
    quint16 type = 1;        // 1 = Entry Mark
    quint16 item = 0;
    quint32 time45k = 0;     // 与所在项 IN/OUT 同轴
};

// 合成 MPLS（PlayList 段在 0x40；有章节时 PlayListMark 段紧随其后）
// PlayItem：u16 项长 + clip[5] + "M2TS"[4] + 3B 连接 + IN[u32] + OUT[u32]
//           + UO 表[8] + still[3] + 附加数据
// Mark 条目（T8 实证）：类型u16 + 项u16 + 时间u32 + PID u16 + 时长u32 = 14B
inline QByteArray buildMpls(const QList<ItemSpec>& items,
                            const QList<MarkSpec>& marks = {},
                            const char* version = "0101")
{
    QByteArray h = QByteArray("MPLS", 4) + QByteArray(version, 4);
    h += be32(0x40);   // playlist_start_address
    h += be32(0);      // playListMark_start_address（下面回填）
    h += be32(0);      // extension_start_address
    h += QByteArray(0x40 - h.size(), 0);

    QByteArray itemsData;
    for (const ItemSpec& it : items) {
        QByteArray body;
        body += QByteArray(it.clip, 5);     // clip file id
        body += QByteArray("M2TS", 4);      // codec id
        body += QByteArray(3, 0);           // 连接信息
        body += be32(it.in45k);
        body += be32(it.out45k);
        body += QByteArray(8, 0);           // UO mask
        body += char(0);                    // still mode
        body += be16(0);                    // still time
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

    QByteArray mk;
    if (!marks.isEmpty()) {
        mk += be32(quint32(2 + marks.size() * 14));   // 段长（计长度字段后）
        mk += be16(quint16(marks.size()));
        for (const MarkSpec& m : marks) {
            mk += be16(m.type) + be16(m.item) + be32(m.time45k)
                + be16(0xFFFF) + be32(0);             // PID + 时长
        }
        const quint32 markAddr = quint32(0x40 + pl.size());
        h[0x0C] = char(markAddr >> 24); h[0x0D] = char(markAddr >> 16);
        h[0x0E] = char(markAddr >> 8);  h[0x0F] = char(markAddr);
    }
    return h + pl + mk;
}

// 章节建议测试的数据构造（不经二进制，直接组 MplsInfo）。
// 时间轴原点取 10:00（实证各盘统一原点偏移，建议值靠相减抵消）。
inline MplsInfo makeChapterInfo(const QList<Cs>& markRelsCs, Cs itemDurCs,
                                int extraMainItems = 0)
{
    MplsInfo info;
    const quint32 origin = 600 * 100 * 450;
    {
        PlayItemSpan main;
        main.clip = QStringLiteral("00001");
        main.in45k = origin;
        main.out45k = origin + ticks(itemDurCs);
        main.durationCs = itemDurCs;
        info.items.append(main);
        info.totalCs = itemDurCs;
    }
    for (int i = 0; i < extraMainItems; ++i) {
        PlayItemSpan extra;   // 一集一项的规整盘：额外正片项 → 章节模式不适用
        extra.clip = QStringLiteral("%1").arg(i + 2, 5, 10, QChar(u'0'));
        extra.in45k = origin;
        extra.out45k = origin + ticks(24 * 6000);
        extra.durationCs = 24 * 6000;
        info.items.append(extra);
        info.totalCs += extra.durationCs;
    }
    for (Cs r : markRelsCs) {
        PlayListMark mk;
        mk.type = 1;
        mk.itemIndex = 0;
        mk.time45k = origin + ticks(r);
        info.marks.append(mk);
    }
    return info;
}

}  // namespace suture::testfix
