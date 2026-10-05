// assparse.hpp —— ASS/SSA 容错解析（架构 §5.2）
// 承载问题：P2（quirks）P3/P4（零时长/Comment 统计口径）P5（垃圾段）
//           P9（Format 列映射）P10（内嵌段告警丢弃）P11（SSA v4 检测）
//           T2（排序索引/maxEnd）T5（Timer）S3 数据准备
// 核心原则：脏数据不崩溃——单行坏跳行告警，段级异常降级为诊断。
#pragma once

#include <QString>
#include <vector>

#include "assdoc.hpp"

namespace suture {

struct ParseOutcome {
    AssDocument doc;
    std::vector<Diagnostic> diags;
    bool ok = false;   // false 仅当文件级不可解析（空文本/完全无段结构）
};

// text 为已解码文本（换行已归一 '\n'；textdec 的产物）
ParseOutcome parseAss(const QString& text);

}  // namespace suture
