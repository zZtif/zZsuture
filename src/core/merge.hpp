// merge.hpp —— 合并引擎（架构 §5.7）
// 承载问题：S1/S2（样式隔离后缀法）S3（未定义引用原样保留+告警）
//           S4（ScriptInfo 六字段决议）S5（特效一字不动只平移时间）
//           S7（{\r} 标签引用同步重写）T3（预填）T6（全局微调）
// 声明式纯函数（D7）：偏移与微调都是参数，重算无状态。
#pragma once

#include <QList>

#include "assdoc.hpp"

namespace suture {

// 预填偏移（T3）：第 N 集起点 = 前一集（偏移 + 其 maxDialogueEnd）逐集累加；
// 第 1 集恒为 0。注意 maxEnd 已排除 Comment/零时长行（P3/P4 口径）。
QList<Cs> computePrefillOffsets(const QList<std::shared_ptr<const AssDocument>>& docs);

// T11：事件基础时刻（偏移+全局+逐行微调+事件自身 start）经时间点累积校正
// 后应追加的平移量。语义：时间点 at 为最终坐标（播放器显示时刻）；基础
// 时刻 T 的事件落在第 k 段（段界按基础坐标换算 b_k = at_k − 前序增量和）
// → 平移 = Σ_{j≤k} δ_j。merge 与 validate 共用，保证 C1/C2 按最终时间校验。
Cs breakpointShiftAt(Cs baseTime, const QList<BreakpointShift>& bps);

// 合并。episodes 按播放顺序；opt 为参数（全局微调/逐行微调/六字段覆盖）。
// 六字段决议：opt 里非空值优先；否则取第一个拥有该字段的文件的值；
// 文件间取值不一致 → S4 告警。
MergeResult merge(QList<EpisodeInput> episodes, const MergeOptions& opt);

}  // namespace suture
