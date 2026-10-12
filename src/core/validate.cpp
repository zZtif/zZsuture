#include "validate.hpp"

#include "merge.hpp"   // breakpointShiftAt（T11：C1/C2 按校正后时间校验）

namespace suture {
namespace {

// C1 阈值：字幕出点比 MPLS 出点晚 5 秒才算异常（片尾字幕略过点属正常容差）
constexpr Cs kC1ThresholdCs = 5 * 100;

}  // namespace

void validate(const QList<EpisodeInput>& episodes, const MergeOptions& opt,
              std::vector<Diagnostic>& out)
{
    if (episodes.isEmpty())
        return;

    // ---- 每集平移后的边界（P3/P4 口径：只算非 Comment 非零时长 Dialogue）----
    // T11：边界同样叠加时间点累积校正（与 merge 同一函数），C1/C2 按最终
    // 时间校验——用户插入的黑场增量不能既改变渲染又不进校验。
    struct Bound {
        Cs minStart = 0, maxEnd = 0, offsetShifted = 0;
        std::optional<Cs> mplsEndShifted;
        bool hasContent = false;
    };
    QList<Bound> bounds;
    bounds.reserve(episodes.size());
    for (const EpisodeInput& ep : episodes) {
        Bound b;
        // A2：分段点为集内语义（挂 fileId、与 event.start 同域）——集起点按
        // fileTime 0 处的增量、事件按自身位置；该集全部点的增量和 = 集界终点
        const Cs base = ep.offset + opt.globalShift;
        b.offsetShifted = base + segmentWarpAt(ep.fileId, 0, opt.segmentPoints);
        Cs totalWarp = 0;
        for (const SegmentPoint& p : opt.segmentPoints)
            if (p.fileId == ep.fileId)
                totalWarp += p.delta;
        b.mplsEndShifted = ep.mplsEnd.has_value()
            ? std::optional<Cs>(*ep.mplsEnd + opt.globalShift + totalWarp)
            : std::nullopt;
        bool first = true;
        for (const EventLine& ev : ep.doc->events) {
            if (ev.isComment || ev.zeroDuration())
                continue;
            const Cs warp = segmentWarpAt(ep.fileId, ev.start, opt.segmentPoints);
            const Cs s = ev.start + base + warp;
            const Cs e = ev.end + base + warp;
            if (first) {
                b.minStart = s;
                b.maxEnd = e;
                first = false;
            } else {
                if (s < b.minStart) b.minStart = s;
                if (e > b.maxEnd)   b.maxEnd = e;
            }
        }
        b.hasContent = !first;
        bounds.push_back(b);
    }

    // ---- C1：时长交叉验证（仅 MPLS 路径有出点数据） ----
    for (int i = 0; i < episodes.size(); ++i) {
        const Bound& b = bounds[i];
        if (!b.hasContent || !b.mplsEndShifted.has_value())
            continue;
        const Cs over = b.maxEnd - *b.mplsEndShifted;
        if (over > kC1ThresholdCs)
            out.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::DurationMismatch,
                QStringLiteral("第 %1 集字幕比该集结束位置晚 %2——开始时间可能"
                               "偏大，或该集字幕与原盘不同源")
                    .arg(i + 1).arg(formatCs(over)), i));
        const Cs early = b.offsetShifted - b.minStart;
        if (early > kC1ThresholdCs)
            out.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::DurationMismatch,
                QStringLiteral("第 %1 集字幕比该集开始位置早 %2——开始时间可能"
                               "偏小")
                    .arg(i + 1).arg(formatCs(early)), i));
    }

    // ---- C2：跨集边界重叠/空档（只看相邻边界，集内同刻多行不检测） ----
    // 注意：单文件没有边界，C2 天然不触发（循环条件即保证）
    for (int i = 0; i + 1 < episodes.size(); ++i) {
        const Bound& prev = bounds[i];
        const Bound& next = bounds[i + 1];
        if (!prev.hasContent || !next.hasContent)
            continue;
        if (prev.maxEnd > next.offsetShifted)
            out.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::BoundaryOverlap,
                QStringLiteral("第 %1 集字幕侵入第 %2 集区间 %3——偏移设置"
                               "可能重叠")
                    .arg(i + 1).arg(i + 2)
                    .arg(formatCs(prev.maxEnd - next.offsetShifted)), i + 1));
        else if (next.offsetShifted - prev.maxEnd > opt.boundaryGapThreshold)
            out.push_back(Diagnostic::make(
                DiagLevel::Info, DiagCode::BoundaryGap,
                QStringLiteral("第 %1 集与第 %2 集之间有 %3 空隙——请确认是否为"
                               "集间黑屏")
                    .arg(i + 1).arg(i + 2)
                    .arg(formatCs(next.offsetShifted - prev.maxEnd)), i + 1));
    }
}

}  // namespace suture
