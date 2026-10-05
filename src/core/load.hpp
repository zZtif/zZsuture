// load.hpp —— 文件装载门面（架构 §5.0，v1.1 新增）
// 职责：读文件 → 编码解码 → 格式判定 → 解析 → 集数识别，产出 LoadedSubtitle。
// 装载编排（格式判定/文件夹展开/过滤/去重）是领域规则，收在 core 而非 GUI，
// 避免 Session 变成第二套逻辑源。
#pragma once

#include <QList>
#include <QString>
#include <QUrl>
#include <optional>
#include <vector>

#include "assdoc.hpp"

namespace suture {

struct LoadOutcome {
    std::optional<LoadedSubtitle> sub;
    std::vector<Diagnostic> diags;   // 装载级诊断（解码/格式）；解析诊断在 sub->loadDiags
    bool ok = false;
};

// 载入单个字幕文件。assignId = 调用方分配的稳定 ID（与列表位置无关）。
// encodingOverride 非空时（UI 手动指定）按指定编码解码并重解析。
LoadOutcome loadSubtitleFile(const QString& path, quint64 assignId,
                             const QString& encodingOverride = QString());

// 拖放统一入口：展开文件夹（非递归）、过滤非字幕文件、去重。
// 返回待装载的路径列表（顺序保持拖入顺序），诊断说明忽略了什么。
struct DropExpansion {
    QList<QString> files;
    std::vector<Diagnostic> diags;
};
DropExpansion expandDrop(const QList<QUrl>& urls);

}  // namespace suture
