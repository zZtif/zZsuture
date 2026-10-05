// epnum.hpp —— 文件名集数识别（F02/P6/P8；架构 §5.6）
// 模式按六组真实样例校准：
//   G1 [01][Hi10p_1080p]     → 括号内纯数字 1-3 位（[00] 特别篇 → 0）
//   G2 [01][CRC32_4702BECB]  → CRC32 段非纯数字自然排除
//   G3 " - 01 [WebRip...]"   → 独立数字段（前后有分隔符边界）
//   G6 【S01】【01】【标题】  → 全角括号；S01 季号非纯数字排除
#pragma once

#include <QString>

namespace suture {

struct EpGuess {
    int  number = -1;     // -1 = 未识别
    bool confident = false;
};

// 文件名 → 集数。识别失败返回 {-1,false}（调用方走自然排序 + 手动调整）
EpGuess guessEpisodeNumber(const QString& fileName);

// 自然排序比较（识别失败时的兜底；数字段按数值比较："2" < "10"）
bool naturalLessThan(const QString& a, const QString& b);

}  // namespace suture
