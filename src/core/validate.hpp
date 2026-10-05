// validate.hpp —— 输出前校验（架构 §5.8）
// C1 时长交叉验证（MPLS 路径）：平移后字幕结束 vs 该集 MPLS 出点，超阈值告警
// C2 跨集边界重叠/空档：只看相邻两集边界——集内同刻多行是常态（双语成对/
//    卡拉OK 逐字），不检测（v1.2 决定 10，G1/G3/G4 实证不误报）
// C3 PlayRes 不一致：已由 merge 的 S4 决议告警覆盖，此处不重复
#pragma once

#include <vector>

#include "assdoc.hpp"

namespace suture {

// 在 merge() 末尾调用（架构 §5.7 第 7 步），诊断并入结果。
// 纯文本运算，基于每集 maxDialogueEnd/排序数据，成本近零。
void validate(const QList<EpisodeInput>& episodes, const MergeOptions& opt,
              std::vector<Diagnostic>& out);

}  // namespace suture
