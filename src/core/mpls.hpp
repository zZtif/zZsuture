// mpls.hpp —— MPLS（蓝光播放列表）解析（T1/T3；架构 §5.5，决策 D4 自解析）
// 二进制布局依据 libbluray mpls 结构（公开格式知识），字段：
//   文件头 0x00 "MPLS"+版本4字节，0x08 起三个大端 u32 段地址
//   PlayList 段：u32 段长 + u16 连接位 + u16 PlayItem 数 + u16 SubPath 数 + PlayItem[]
//   PlayItem：u32 项长，+17 处 u32 IN_time，+21 处 u32 OUT_time（45kHz tick）
//   多角度项在 OUT 之后有附加数据——靠"项长"字段整体跳过，不需解析角度细节
// 防御式读取：任何越界/长度不符 → 明确诊断放弃，绝不输出可疑偏移（宁可没有）。
#pragma once

#include <QByteArray>
#include <QList>
#include <optional>
#include <vector>

#include "diag.hpp"
#include "time.hpp"

namespace suture {

struct PlayItemSpan {
    quint32 in45k = 0, out45k = 0;   // 45kHz tick（大端原始值）
    Cs durationCs = 0;               // (OUT-IN) 换算厘秒
};

struct MplsInfo {
    QString version;                 // 如 "0101" / "0200"
    QList<PlayItemSpan> items;       // 播放顺序
    Cs totalCs = 0;
};

struct MplsOutcome {
    std::optional<MplsInfo> info;
    std::vector<Diagnostic> diags;   // 失败时至少一条 Error
};

// 解析 MPLS 文件字节流
MplsOutcome parseMpls(const QByteArray& raw);

// ---- D10：PlayItem ↔ 集数映射 ----
// 策略：时长 < 60s 的 PlayItem 视为非正片（Logo/菜单）；过滤后数量 == 集数 →
// 顺序对齐（每集起点 = 其前所有 item 含 Logo 的累计时长）。
// 数量不等 → 空返回 + 告警（用户在偏移列手动兜底——该列本就可编辑）。
struct MplsMapping {
    QList<Cs> offsets;       // 每集起点（厘秒，含前置 Logo）
    QList<Cs> episodeEnds;   // 每集出点（起点 + 该集正片时长；C1 交叉验证用）
    bool ok = false;
};
MplsMapping mapItemsToEpisodes(const MplsInfo& info, int episodeCount,
                               std::vector<Diagnostic>& diags);

}  // namespace suture
