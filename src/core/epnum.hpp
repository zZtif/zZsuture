// epnum.hpp —— 文件名集数识别（F02/P6/P8；架构 §5.6）
// 模式按六组真实样例校准：
//   G1 [01][Hi10p_1080p]     → 括号内纯数字 1-3 位（[00] 特别篇 → 0）
//   G2 [01][CRC32_4702BECB]  → CRC32 段非纯数字自然排除
//   G3 " - 01 [WebRip...]"   → 独立数字段（前后有分隔符边界）
//   G6 【S01】【01】【标题】  → 全角括号；S01 季号非纯数字排除
#pragma once

#include <QString>

#include "assdoc.hpp"

namespace suture {

struct EpGuess {
    int  number = -1;     // -1 = 未识别
    bool confident = false;
};

// 文件名 → 集数。识别失败返回 {-1,false}（调用方走自然排序 + 手动调整）
EpGuess guessEpisodeNumber(const QString& fileName);

// P12/F16：字幕内容 → 集数（文件名识别失败时的第二信号）。
// 依据（实证：死后文盘内 2014 年外挂的 Title 即为 "…Episode 1"）：Script
// Info Title 与事件文本中的集数模式（第X話/话、Episode X、EPX），Title 命中
// 加权 3（作者刻意声明），全文取总分最高者——次回预告会提到下一集集号，
// 逐行取值会被带偏。无标记 / 并列最高 → 不识别（回退自然排序，不硬猜）。
EpGuess guessEpisodeFromContent(const AssDocument& doc);

// 自然排序比较（识别失败时的兜底；数字段按数值比较："2" < "10"）
bool naturalLessThan(const QString& a, const QString& b);

}  // namespace suture
