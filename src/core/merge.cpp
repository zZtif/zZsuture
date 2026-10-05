#include "merge.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <functional>

#include "validate.hpp"

namespace suture {
namespace {

// ---- S7：{\r} 标签引用重写 ----
// ASS 特效标签 \rStyleName 在 Text 内部按名引用样式（卡拉OK 二段着色常用）。
// 只重写指向"本文件已定义样式"的名字；裸 \r（重置为本行样式）与未定义名字不动。
// 匹配规则：\r 之后直到 '}' 或 '\' 的连续字符视为样式名。
int rewriteRTags(QString& text, const QHash<QString, QString>& renameMap)
{
    static const QRegularExpression re(QStringLiteral("\\\\r([^\\\\}]*)"));
    int rewritten = 0;
    qsizetype from = 0;
    QRegularExpressionMatch m;
    while ((m = re.match(text, from)).hasMatch()) {
        const QString name = m.captured(1);
        const auto it = renameMap.constFind(name);
        if (it != renameMap.constEnd()) {
            text.replace(m.capturedStart(1), name.size(), *it);
            from = m.capturedStart(1) + it->size();
            ++rewritten;
        } else {
            from = m.capturedEnd(0);
        }
    }
    return rewritten;
}

// ---- S4：字符串字段决议 ----
// optVal 非空优先；否则取第一个拥有该字段的文件的值；文件间不一致 → conflict。
struct FieldResolution {
    QString value;
    bool    conflict = false;
};
FieldResolution resolveField(const QString& optVal,
                             const QList<EpisodeInput>& eps,
                             const std::function<std::optional<QString>(
                                 const ScriptInfo&)>& getter)
{
    FieldResolution r;
    if (!optVal.isEmpty())
        r.value = optVal.trimmed();
    for (const EpisodeInput& ep : eps) {
        const auto v = getter(ep.doc->info);
        if (!v.has_value() || v->trimmed().isEmpty())
            continue;
        const QString t = v->trimmed();
        if (r.value.isEmpty())
            r.value = t;
        else if (r.value.compare(t, Qt::CaseInsensitive) != 0)
            r.conflict = true;
    }
    return r;
}

// ---- S4：分辨率字段（PlayRes/LayoutRes 成对）决议 ----
struct SizeResolution {
    QString w, h;
    bool    conflict = false;
};
SizeResolution resolveSize(
    const QList<EpisodeInput>& eps,
    std::optional<QSize> (ScriptInfo::*field))
{
    SizeResolution r;
    for (const EpisodeInput& ep : eps) {
        const auto v = (ep.doc->info.*field);
        if (!v.has_value() || !v->isValid())
            continue;
        const QString tw = QString::number(v->width());
        const QString th = QString::number(v->height());
        if (r.w.isEmpty()) {
            r.w = tw;
            r.h = th;
        } else if (r.w != tw || r.h != th) {
            r.conflict = true;
        }
    }
    return r;
}

// 六关键字段中"文件间取值"是否已在第一个非空值处（供告警消息列出首值）
QString firstValueOf(const QList<EpisodeInput>& eps,
                     const std::function<std::optional<QString>(
                         const ScriptInfo&)>& getter)
{
    for (const EpisodeInput& ep : eps) {
        const auto v = getter(ep.doc->info);
        if (v.has_value() && !v->trimmed().isEmpty())
            return v->trimmed();
    }
    return QString();
}

}  // namespace

QList<Cs> computePrefillOffsets(const QList<std::shared_ptr<const AssDocument>>& docs)
{
    QList<Cs> out;
    out.reserve(docs.size());
    Cs running = 0;
    for (const auto& d : docs) {
        out.append(running);
        running += d ? d->maxDialogueEnd : 0;
    }
    return out;
}

MergeResult merge(QList<EpisodeInput> episodes, const MergeOptions& opt)
{
    MergeResult result;
    auto& diags = result.diags;
    AssDocument& merged = result.merged;

    if (episodes.isEmpty()) {
        diags.push_back(Diagnostic::make(
            DiagLevel::Error, DiagCode::ParseQuirk,
            QStringLiteral("没有输入文件，无法合并")));
        return result;
    }

    // ---------- 1. ScriptInfo 六字段决议（S4：告警 + 默认取第一个值） ----------
    static const auto getWrap = [](const ScriptInfo& i) { return i.wrapStyle; };
    static const auto getSbs  = [](const ScriptInfo& i) { return i.sbs; };
    static const auto getYcbcr = [](const ScriptInfo& i) { return i.ycbcrMatrix; };

    const FieldResolution wrap   = resolveField(opt.resolvedWrapStyle, episodes, getWrap);
    const FieldResolution sbs    = resolveField(opt.resolvedSbs, episodes, getSbs);
    const FieldResolution ycbcr  = resolveField(opt.resolvedYcbcr, episodes, getYcbcr);
    const SizeResolution playRes   = resolveSize(episodes, &ScriptInfo::playRes);
    const SizeResolution layoutRes = resolveSize(episodes, &ScriptInfo::layoutRes);

    if (wrap.conflict) {
        QStringList values;
        for (const EpisodeInput& ep : episodes) {
            const auto x = getWrap(ep.doc->info);
            if (x.has_value() && !x->trimmed().isEmpty())
                values << x->trimmed();
        }
        values.removeDuplicates();
        diags.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::ScriptInfoConflict,
            QStringLiteral("WrapStyle 各集不一致（已取 %1，全部取值：%2）")
                .arg(wrap.value, values.join(u'/'))));
    }
    if (sbs.conflict)
        diags.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::ScriptInfoConflict,
            QStringLiteral("ScaledBorderAndShadow 各集不一致（已取 %1）——"
                           "该项直接影响描边缩放，混拼时描边粗细可能突变")
                .arg(sbs.value)));
    if (ycbcr.conflict)
        diags.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::ScriptInfoConflict,
            QStringLiteral("YCbCr Matrix 各集不一致（已取 %1）").arg(ycbcr.value)));
    if (playRes.conflict)
        diags.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::ScriptInfoConflict,
            QStringLiteral("PlayRes 各集不一致（已取 %1×%2）——混拼时特效"
                           "字幕可能错位").arg(playRes.w, playRes.h)));
    if (layoutRes.conflict)
        diags.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::ScriptInfoConflict,
            QStringLiteral("LayoutRes 各集不一致（已取 %1×%2）")
                .arg(layoutRes.w, layoutRes.h)));

    // ---------- 2. 样式隔离（S1/S2：后缀法 ED_jap@3） ----------
    int totalStyles = 0;
    QList<QHash<QString, QString>> renameMaps;   // 每文件：原名 → 隔离名
    for (int i = 0; i < episodes.size(); ++i) {
        const int suffix = i + 1;   // ED_jap@3 = 列表第 3 个文件
        QHash<QString, QString> map;
        QHash<QString, int> dupCount;   // 同文件重名 → 附加 #2（解析期已告警）
        for (const StyleDef& st : episodes[i].doc->styles) {
            StyleDef out = st;
            out.outputName = st.originalName + QStringLiteral("@%1").arg(suffix);
            const int seen = ++dupCount[st.originalName];
            if (seen > 1)
                out.outputName += QStringLiteral("#%1").arg(seen);
            map.insert(st.originalName, out.outputName);
            merged.styles.push_back(std::move(out));
            ++totalStyles;
        }
        renameMaps.append(map);
    }
    diags.push_back(Diagnostic::make(
        DiagLevel::Info, DiagCode::StyleIsolated,
        QStringLiteral("已按源文件隔离 %1 个样式（后缀 @N，N=文件序号）")
            .arg(totalStyles)));

    // v4 文件参与合并的告警（P11）
    for (int i = 0; i < episodes.size(); ++i)
        if (episodes[i].doc->ssaV4)
            diags.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::UnsupportedSsaV4,
                QStringLiteral("第 %1 个文件是 SSA v4.00 老格式，其样式行"
                               "字段序与 V4+ 不同，合并输出可能错位")
                    .arg(i + 1), i));

    // ---------- 3. 逐行微调索引（稳定 fileId + eventIndex → delta） ----------
    QHash<QPair<quint64, int>, Cs> adjustMap;
    for (const LineAdjustment& a : opt.adjustments)
        adjustMap.insert({a.fileId, a.eventIndex}, a.delta);

    // ---------- 4. 事件重写 + 平移 + 拼接 ----------
    QHash<QPair<int, QString>, int> undefinedRefCount;   // S3 按 (文件,样式名) 去重
    int rTagTotal = 0;
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeInput& ep = episodes[i];
        const QHash<QString, QString>& map = renameMaps[i];

        // 每集起点注释标记（Comment 事件：重解析可存活、不进统计）
        {
            EventLine marker;
            marker.isComment = true;
            marker.start = marker.end = ep.offset + opt.globalShift;
            marker.text = QStringLiteral("-- EP%1 starts at %2 (offset %3%4) --")
                .arg(i + 1, 2, 10, QChar(u'0'))
                .arg(formatCs(ep.offset))
                .arg(ep.offset < 0 ? QStringLiteral("-") : QStringLiteral("+"))
                .arg(formatCs(ep.offset < 0 ? -ep.offset : ep.offset));
            merged.events.push_back(std::move(marker));
        }

        for (int e = 0; e < int(ep.doc->events.size()); ++e) {
            EventLine ev = ep.doc->events[size_t(e)];
            // 样式引用重写（S3：未定义引用原样保留）
            if (map.contains(ev.style)) {
                ev.style = map.value(ev.style);
            } else if (!ev.style.isEmpty()) {
                ++undefinedRefCount[{i, ev.style}];
            }
            // S7：Text 内 {\r} 引用同步重写（只动名字，其余字符零接触）
            rTagTotal += rewriteRTags(ev.text, map);
            // 平移（S5：只动 Start/End；Comment/零时长行同样平移，P3/P4）
            const Cs delta = ep.offset + opt.globalShift +
                             adjustMap.value({ep.fileId, e}, 0);
            ev.start += delta;
            ev.end += delta;
            merged.events.push_back(std::move(ev));
        }
    }

    for (auto it = undefinedRefCount.constBegin();
         it != undefinedRefCount.constEnd(); ++it) {
        diags.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::UndefinedStyleRef,
            QStringLiteral("第 %1 个文件有 %2 行引用未定义样式 \"%3\"，"
                           "已原样保留（与源文件播放行为一致）")
                .arg(it.key().first + 1).arg(it.value()).arg(it.key().second),
            it.key().first));
    }
    if (rTagTotal > 0)
        diags.push_back(Diagnostic::make(
            DiagLevel::Info, DiagCode::RTagRewritten,
            QStringLiteral("已同步重写 %1 处 {\\r} 样式引用").arg(rTagTotal)));

    // ---------- 5. 派生数据 ----------
    for (const EventLine& ev : merged.events)
        if (!ev.isComment && !ev.zeroDuration())
            merged.maxDialogueEnd = std::max(merged.maxDialogueEnd, ev.end);
    merged.sortedByStart.reserve(int(merged.events.size()));
    for (int i = 0; i < int(merged.events.size()); ++i)
        merged.sortedByStart.append(i);
    std::stable_sort(merged.sortedByStart.begin(), merged.sortedByStart.end(),
                     [&merged](int a, int b) {
                         return merged.events[size_t(a)].start <
                                merged.events[size_t(b)].start;
                     });

    // ---------- 6. 校验并入诊断（架构 §5.7 第 7 步：C1/C2） ----------
    validate(episodes, opt, diags);

    // ---------- 7. ScriptInfo 输出条目 ----------
    auto& entries = merged.info.entries;
    entries.push_back({QStringLiteral("; SUture 合并输出（%1 个文件）")
                           .arg(episodes.size()), QString()});
    entries.push_back({QStringLiteral("ScriptType"), QStringLiteral("v4.00+")});
    if (!playRes.w.isEmpty()) {
        entries.push_back({QStringLiteral("PlayResX"), playRes.w});
        entries.push_back({QStringLiteral("PlayResY"), playRes.h});
    }
    if (!layoutRes.w.isEmpty()) {
        entries.push_back({QStringLiteral("LayoutResX"), layoutRes.w});
        entries.push_back({QStringLiteral("LayoutResY"), layoutRes.h});
    }
    if (!wrap.value.isEmpty())
        entries.push_back({QStringLiteral("WrapStyle"), wrap.value});
    if (!sbs.value.isEmpty())
        entries.push_back({QStringLiteral("ScaledBorderAndShadow"), sbs.value});
    if (!ycbcr.value.isEmpty())
        entries.push_back({QStringLiteral("YCbCr Matrix"), ycbcr.value});
    // 其余元信息：第一个文件的条目原样保留；后续文件的差异条目合并为注释（S4）
    QSet<QString> seenKeys;
    for (int i = 0; i < episodes.size(); ++i) {
        for (const auto& e : episodes[i].doc->info.entries) {
            if (e.key.startsWith(u';'))
                continue;
            const QString low = e.key.toLower();
            // 六关键字段/ScriptType/Timer 已决议或有意省略，不重复带入
            if (low == QStringLiteral("scripttype") ||
                low == QStringLiteral("timer") ||
                low.startsWith(QStringLiteral("playres")) ||
                low.startsWith(QStringLiteral("layoutres")) ||
                low == QStringLiteral("wrapstyle") ||
                low == QStringLiteral("scaledborderandshadow") ||
                low == QStringLiteral("ycbcr matrix"))
                continue;
            const QString sig = low + u'=' + e.value;
            if (i == 0) {
                entries.push_back(e);
                seenKeys.insert(sig);
            } else if (!seenKeys.contains(sig)) {
                entries.push_back({QStringLiteral("; [EP%1] %2: %3")
                                       .arg(i + 1, 2, 10, QChar(u'0'))
                                       .arg(e.key, e.value),
                                   QString()});
                seenKeys.insert(sig);
            }
        }
    }

    return result;
}

}  // namespace suture
