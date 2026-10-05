// diag.hpp —— 统一诊断对象（架构 §4/§5.1）
// 三原则的载体：不崩溃（诊断代替异常）、不静默篡改（每个篡改类动作必须伴随诊断）、
// 可疑之处告警（Warning/Info 分级）。诊断码与 设计/待解决问题清单.md 的 ID 挂钩，
// UI 可按 ID 分组、点击定位。
#pragma once

#include <QString>
#include <vector>

namespace suture {

enum class DiagLevel { Info, Warning, Error };

enum class DiagCode {
    // ---- 编码/装载（P1/P6 等）----
    EncodingUncertain,   // P1  编码判定不确定，默认按 X 处理
    EncodingHeuristic,   // P1  无 BOM 走了启发式（零字节奇偶/GBK-Big5 评分）
    FileUnreadable,      // 文件读失败/为空
    UnknownFormat,       // 内容嗅探无法识别字幕格式
    FormatNotSupported,  // 格式识别但本阶段不支持（如 SRT 在阶段 4 前）
    DuplicateFile,       // 重复文件被拒绝
    NonSubtitleIgnored,  // 拖入的无关文件被忽略
    EpisodeUnrecognized, // P6  集数识别失败，按自然排序

    // ---- 解析容错（P2/P3/P4/P5/P9/P10/P11）----
    ParseQuirk,          // P2  老工具链写法被容错
    MalformedLine,       //      字段缺失/时间非法，该行跳过
    ZeroDurationIgnored, // P3  零时长行不进统计
    CommentIgnoredStat,  // P4  Comment 行不进统计
    GarbageDropped,      // P5  [Aegisub Project Garbage] 段丢弃
    EmbeddedDropped,     // P10 [Fonts]/[Graphics]/[Aegisub Extradata] 段丢弃
    UnsupportedSsaV4,    // P11 SSA v4.00 老格式：告警 + 原样无损保留
    StyleNameDuplicate,  //      同文件内重名样式
    TimerNot100,         // T5  Timer ≠ 100，时间轴可能被拉伸

    // ---- 合并（阶段 2 使用，先占位）----
    UndefinedStyleRef,   // S3  事件引用未定义样式
    ScriptInfoConflict,  // S4  六关键字段冲突
    RTagRewritten,       // S7  {\r} 样式引用已同步重写
    StyleIsolated,       // S1/S2 已隔离重命名 N 个样式

    // ---- 校验/MPLS（阶段 3 使用，先占位）----
    DurationMismatch,    // C1
    BoundaryOverlap,     // C2
    BoundaryGap,         // C2
    PlayResMismatch,     // C3
    MplsUnsupported,     //      MPLS 结构异常，降级手动
};

// 诊断码 → 问题清单 ID（无对应条目返回 "—"；UI 徽标与验收对照用）
const char* diagCodeId(DiagCode code);

struct Diagnostic {
    DiagLevel level = DiagLevel::Info;
    DiagCode  code  = DiagCode::ParseQuirk;
    int       fileIndex = -1;   // 哪个输入文件（-1 = 全局）
    int       line      = -1;   // 源文件行号（1 基，可用则填）
    QString   message;          // 人话描述，含具体数值

    static Diagnostic make(DiagLevel l, DiagCode c, QString msg,
                           int fileIdx = -1, int lineNo = -1)
    { return Diagnostic{l, c, fileIdx, lineNo, std::move(msg)}; }
};

}  // namespace suture
