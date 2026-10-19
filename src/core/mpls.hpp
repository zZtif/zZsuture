// mpls.hpp —— MPLS（蓝光播放列表）解析（T1/T3/T8；架构 §5.5，决策 D4 自解析）
// 布局实证自真实原盘逆向验证，字节账目精确闭合：
//   文件头 0x00 "MPLS"+版本4字节，0x08 起三个大端 u32 段地址
//   PlayList 段：u32 段长（计长度字段之后的字节数）+ u16 保留 + u16 PlayItem 数
//                + u16 SubPath 数 + PlayItem[] + SubPath[]
//   PlayItem：u16 项长（计该项长字段之后的字节数）+ clip 名[5] + "M2TS"[4]
//             + 3 字节连接信息 + IN_time[u32] + OUT_time[u32] + UO 表/STN 表等
//             → IN 在项内 +14，OUT 在项内 +18；下一项 = 项起点 + 2 + 项长
//   PlayListMark 段（T8，三盘原始字节实证 2026-10-07）：u32 段长 + u16 章节数
//             + N 个 14 字节条目 = 类型u16 + 所属项u16 + 时间u32 + PID u16
//             + 时长u32。时间为 45kHz tick，与 IN/OUT 同一 clip 时间轴。
//   时间单位 45kHz tick（1 cs = 450 tick）
// 防御式读取：任何越界/长度不符 → 明确诊断放弃，绝不输出可疑偏移（宁可没有）。
#pragma once

#include <QByteArray>
#include <QList>
#include <QStringList>
#include <optional>
#include <vector>

#include "diag.hpp"
#include "time.hpp"

namespace suture {

struct PlayItemSpan {
    QString clip;                    // clip 文件名 5 字符（"00001" → 00001.m2ts；F07 同名导出用）
    quint32 in45k = 0, out45k = 0;   // 45kHz tick（大端原始值）
    Cs durationCs = 0;               // (OUT-IN) 换算厘秒
};

// T8：章节标记。time45k 与所在项的 IN/OUT 同轴——换算"m2ts 文件内位置"
// 用 time45k − 所在项 in45k，各盘统一的时间轴原点偏移（1:09:59 / 10:00 等）
// 在相减中自然抵消，无需解析 CLIPINF。
struct PlayListMark {
    int itemIndex = 0;      // 所属 PlayItem（0 基）
    quint32 time45k = 0;
    quint16 type = 0;       // 1 = Entry Mark（普通章节；其余类型保留原值不参与建议）
};

struct MplsInfo {
    QString version;                 // 如 "0101" / "0200"
    QList<PlayItemSpan> items;       // 播放顺序
    QList<PlayListMark> marks;       // 章节（PlayListMark 段；无段则为空）
    Cs totalCs = 0;
};

struct MplsOutcome {
    std::optional<MplsInfo> info;
    std::vector<Diagnostic> diags;   // 失败时至少一条 Error
};

// 解析 MPLS 文件字节流
MplsOutcome parseMpls(const QByteArray& raw);

// ---- D10：PlayItem ↔ 集数映射 ----
// 策略：时长 < 60s 的 PlayItem 判为非正片（Logo/菜单）；字幕第 1 集对应
// startMain 个正片项（0 基）——支持"整季连播链只合并中间几集"的场景
// （实证：GTO 全系列 44 项链 + 用户逐 4 集字幕）。每集起点 = 从链首（含
// Logo 与被跳过的前序正片）到该集项的累计时长——MPLS 播放的时间轴从链首
// 起算，PowerDVD 章节跳集不改变这一点。
// 起始项 + 集数超出正片总数 → 空返回 + 告警（偏移列手动兜底）。
struct MplsMapping {
    QList<Cs> offsets;       // 每集起点（厘秒）
    QList<Cs> episodeEnds;   // 每集出点（起点 + 该集正片时长；C1 交叉验证用）
    QList<int> mainItems;    // 正片项序号（全表；供 UI 选择起始集）
    bool ok = false;
};
MplsMapping mapItemsToEpisodes(const MplsInfo& info, int episodeCount,
                               int startMain, std::vector<Diagnostic>& diags);

// ---- T8/T10：章节分集建议（v1.4 场景：多集一个 m2ts，直接开文件播放）----
// 适用前提：恰一个正片 PlayItem（多集装在一个 m2ts，黑契/死后文型）且其
// Entry Mark 章节非空。多个正片项（一集一项的规整盘 / GTO 长链）返回
// ok=false，调用方回落 PlayItem 映射。
struct ChapterMapping {
    bool ok = false;
    QString clip;               // 多集 m2ts 的 clip 名（F07 同名导出用）
    QList<Cs> offsets;         // 每集分界点（m2ts 文件内相对位置，第 1 集恒为 0）
    QList<Cs> episodeEnds;      // 建议集界 = 下一集分界点（末集 = 项时长），C1 用
    QStringList epBasis;       // 每集依据文本（解析页悬停："链式预估 X → 吸附
                               // 章节 Y" / "无章节佐证请人工核对"）
};

// 链式分界点（所有者设计并实测验证，整改文档 一.2）：
//   ① 第 1 集分界点 = 文件头（多集 m2ts 第一集在 0:00，三盘实证）
//   ② 第 k+1 集预估 = 第 k 集分界点 + 第 k 集字幕最晚结束（结束最晚的
//      一行有效台词，不区分样式——ED 歌词/下集预告/对话谁最晚取谁）
//   ③ 向后就近吸附章节节点（预估位之后 60 秒内——分界节点必然在最后
//      一句台词之后，向后搜索才会引入歧义）；吸不到保持预估值 + 告警
// 旧机制（等分位/90 秒签名加分/DP/ED 锚定）已全部废除——从盘单侧猜分集
// 点的路线被 DISC2 实测证伪。14/14 分界点三盘验证 + 所有者端到端实测
// （按建议合成字幕，对轴无偏差）。
ChapterMapping suggestChapterStarts(const MplsInfo& info, int episodeCount,
                                    const QList<Cs>& subLastEnds,  // 每集字幕最晚结束
                                    std::vector<Diagnostic>& diags);

}  // namespace suture
