#include "validate.hpp"

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
    struct Bound {
        Cs minStart = 0, maxEnd = 0, offsetShifted = 0;
        std::optional<Cs> mplsEndShifted;
        bool hasContent = false;
    };
    QList<Bound> bounds;
    bounds.reserve(episodes.size());
    for (const EpisodeInput& ep : episodes) {
        Bound b;
        b.offsetShifted = ep.offset + opt.globalShift;
        b.mplsEndShifted = ep.mplsEnd.has_value()
            ? std::optional<Cs>(*ep.mplsEnd + opt.globalShift)
            : std::nullopt;
        bool first = true;
        for (const EventLine& ev : ep.doc->events) {
            if (ev.isComment || ev.zeroDuration())
                continue;
            const Cs s = ev.start + b.offsetShifted;
            const Cs e = ev.end + b.offsetShifted;
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
                QStringLiteral("第 %1 集字幕比 MPLS 出点晚 %2——偏移可能偏大"
                               "或该集字幕时轴与原盘不同源")
                    .arg(i + 1).arg(formatCs(over)), i));
        const Cs early = b.offsetShifted - b.minStart;
        if (early > kC1ThresholdCs)
            out.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::DurationMismatch,
                QStringLiteral("第 %1 集字幕比 MPLS 入点早 %2——偏移可能偏小")
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
                QStringLiteral("第 %1 集与第 %2 集之间有 %3 空档——请确认偏移"
                               "或集间黑场是否正常")
                    .arg(i + 1).arg(i + 2)
                    .arg(formatCs(next.offsetShifted - prev.maxEnd)), i + 1));
    }
}

}  // namespace suture
