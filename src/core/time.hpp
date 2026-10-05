// time.hpp —— 厘秒整数时间（架构 §4；问题 T4：无浮点漂移）
// 解析算法采纳自 Aegisub libaegisub/ass/time.cpp（BSD 许可，已改写为厘秒版）：
// 逐字符状态机——':' 进位×60、'.' 或 ',' 进小数、非数字忽略、无小数按整秒。
#pragma once

#include <QString>
#include <QStringView>
#include <cstdint>
#include <optional>

namespace suture {

// centiseconds（厘秒）。ASS 标准时间精度即厘秒，全程整数运算。
using Cs = std::int64_t;

// ASS 时间上限同 Aegisub：10 小时（防止脏数据撑爆输出格式）
inline constexpr Cs kMaxCs = 10LL * 60 * 60 * 100 - 1;

// "H:MM:SS.CC" → 厘秒。容错：非数字字符忽略、',' 与 '.' 等价、无小数按整秒。
// 解析后钳制到 [0, kMaxCs]（与 Aegisub 行为一致，负值在 ASS 中非法）。
// 无法解析（无任何数字）返回 std::nullopt。
std::optional<Cs> parseTimeToCs(QStringView s);

// 厘秒 → "H:MM:SS.CC"（小时一位数，ASS 规范写法；进位/跨小时由整除保证正确）
QString formatCs(Cs v);

// MPLS 45kHz tick → 厘秒，四舍五入（1 cs = 450 tick，误差 ≤0.5cs < 1/4 帧）
inline constexpr Cs ticks45kToCs(std::uint32_t ticks)
{
    return (static_cast<Cs>(ticks) * 2 + 450) / 900;
}

}  // namespace suture
