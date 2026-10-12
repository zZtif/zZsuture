// assdoc.hpp —— 核心数据结构（架构 §4）
// 设计要点：
//  · EventLine.text 为特效原文，一字不动（S5）；只有时间/样式名会被改写
//  · StyleDef.rawLine 保留原始 Style 行，输出时仅替换名字段（D3 外科手术式）
//  · ScriptInfo 保持原始条目顺序（无损往返），同时抽取六关键字段供冲突检测（S4）
#pragma once

#include <QList>
#include <QPair>
#include <QSize>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

#include "diag.hpp"
#include "time.hpp"

namespace suture {

// ============ 事件行（Dialogue / Comment）============
struct EventLine {
    bool    isComment = false;  // Comment 行：保留+平移，不进统计（P4）
    Cs      start = 0, end = 0;
    int     layer = 0;
    QString style;              // 样式引用名（merge 阶段重写为隔离名）
    QString actor, effect;
    QString margins[3];         // 原样字符串（"0000"/"0" 都原样进，输出规范化）
    QString text;               // ★ 特效原文，一字不动（S5）
    int     sourceLine = 0;     // 源文件行号（1 基），诊断定位

    bool zeroDuration() const { return start == end; }  // P3：不进统计
};

// ============ 样式定义 ============
struct StyleDef {
    QString originalName;   // 源文件中的名字
    QString outputName;     // 输出名（解析时 = originalName；merge 隔离时改写）
    QString rawLine;        // 原始 Style 行，输出时仅替换第一个字段
};

// ============ Script Info ============
// entries 保持源文件顺序（含 ';' 注释行），六关键字段额外抽取为类型化视图。
struct ScriptInfoEntry {
    QString key;    // 原样键名（含 ';' 注释行时 key 为整行、value 为空）
    QString value;
};

struct ScriptInfo {
    std::vector<ScriptInfoEntry> entries;         // 原始条目（无损往返）
    // 六关键字段（S4 冲突检测用；从 entries 抽取，键名大小写不敏感）
    std::optional<QString> scriptType, wrapStyle, sbs, ycbcrMatrix;
    std::optional<QSize>   playRes, layoutRes;
    QString timer;        // T5：≠100.0000 即告警（空 = 未出现）

    std::optional<QString> value(const QString& key) const;  // 大小写不敏感查找
};

// ============ 整个字幕文档 ============
struct AssDocument {
    ScriptInfo        info;
    std::vector<StyleDef> styles;
    std::vector<EventLine> events;      // 保持源文件原始行序（输出行序决定）

    // —— 解析即建的派生数据（T2：事件乱序为常态）——
    Cs maxDialogueEnd = 0;      // 全扫非 Comment 非零时长 Dialogue 的最大 End
    QList<int> sortedByStart;   // 稳定排序索引（校验/可视化用；不用于输出）

    // —— 文件元信息 ——
    QString sourcePath;
    QString encodingName;       // 诊断/界面显示
    bool    ssaV4 = false;      // P11：SSA v4.00 老格式标记（[V4 Styles] / ScriptType v4.00）
    QString stylesFormatLine;   // 样式段原始 Format 行（v4 保留原样；v4+ 缺失时填标准行）
};

// ============ 装载产物（load 门面，架构 §5.0）============
struct LoadedSubtitle {
    quint64     id = 0;         // 装载时分配的稳定 ID，与列表位置无关
    QString     path;
    AssDocument doc;
    std::vector<Diagnostic> loadDiags;
    int         epNumber = -1;  // epnum 识别结果，-1 = 未识别
    bool        epConfident = false;
};

// ============ 合并任务参数（阶段 2 使用；F03/F06 统一模型）============
struct EpisodeInput {
    std::shared_ptr<const AssDocument> doc;  // 共享而非拷贝——重算是常态路径
    Cs      offset = 0;
    quint64 fileId = 0;   // 装载时分配的稳定文件 ID（微调绑定用，与列表位置无关）
    std::optional<Cs> mplsEnd;   // 该集 MPLS 出点（绝对时刻；C1 交叉验证用）
    enum class OffsetSource { Mpls, Manual, Prefill, Adjusted, Chapter, Timeline };
    OffsetSource offsetSource = OffsetSource::Prefill;
};

struct LineAdjustment {   // F06 单行微调：也是参数，不是修改（D7）
    quint64 fileId;       // 绑定稳定文件 ID——用户换序后微调不致错绑
    int     eventIndex;
    Cs      delta;
};

// T11/A2 分段点（时间轴交互重定义，需求 v2.0）：归属到集（文件）的时间点，
// 从该点到该集结束、该集的内容整体平移 delta——不跨集、无隐式联动。
// at 用该文件自身时间轴（与 event.start 同域），集的偏移移动整集、分段点
// 只动集内后半段。常量平移不动行内相对时序，卡拉OK \k 安全。
struct SegmentPoint {
    quint64 fileId = 0;   // 归属文件（稳定 ID）
    Cs at = 0;            // 该文件自身时间轴上的位置
    Cs delta = 0;         // 从该点到该集结束追加的平移量（可负）
};

struct MergeOptions {
    Cs globalShift = 0;                 // F06 全局微调
    QList<SegmentPoint> segmentPoints;  // T11/A2 分段点（集内语义）
    // C2 空档阈值：预填/PlayItem 模式下无缝连播应为 0（默认 2 秒）；章节模式
    // 的集间黑场/ED 空白是常态，由 Session 切为 5 分钟（T8 场景差异化）
    Cs boundaryGapThreshold = 2 * 100;
    QList<LineAdjustment> adjustments;  // F06 逐行微调
    // S4 六字段决议（默认取第一个文件值，UI 可改）
    QString resolvedWrapStyle, resolvedSbs, resolvedYcbcr;
    QSize   resolvedPlayRes;
};

struct MergeResult {
    AssDocument merged;
    std::vector<Diagnostic> diags;
};

}  // namespace suture
