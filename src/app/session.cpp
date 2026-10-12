#include "session.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

#include "asswrite.hpp"
#include "epnum.hpp"
#include "mpls.hpp"

namespace suture {

Session::Session(QObject* parent)
    : QObject(parent)
{
}

void Session::addUrls(const QList<QUrl>& urls)
{
    const DropExpansion drop = expandDrop(urls);
    dropDiags_ = QList<Diagnostic>(drop.diags.begin(), drop.diags.end());

    bool added = false;
    for (const QString& path : drop.files) {
        LoadOutcome l = loadSubtitleFile(path, nextId_++);
        if (!l.ok) {
            dropDiags_.append(QList<Diagnostic>(l.diags.begin(), l.diags.end()));
            continue;
        }
        Entry e;
        e.sub = std::make_shared<LoadedSubtitle>(std::move(*l.sub));
        e.doc = std::make_shared<const AssDocument>(std::move(e.sub->doc));
        entries_.push_back(std::move(e));
        added = true;
    }
    if (!added && dropDiags_.isEmpty())
        return;

    // F02：按集数排序。全部识别成功 → 按集数；否则自然排序兜底（P6）
    const bool allConfident = std::all_of(
        entries_.cbegin(), entries_.cend(),
        [](const Entry& e) { return e.sub->epConfident; });
    std::stable_sort(entries_.begin(), entries_.end(),
                     [allConfident](const Entry& a, const Entry& b) {
                         if (allConfident)
                             return a.sub->epNumber < b.sub->epNumber;
                         return naturalLessThan(QFileInfo(a.sub->path).fileName(),
                                                QFileInfo(b.sub->path).fileName());
                     });
    // 偏移来源优先级：MPLS 生效则重新映射（集数可能变了），否则预填
    if (mpls_)
        analyzeMpls();
    else
        recomputePrefill();
    checkEpisodeOrder();
    undoStack_.clear();   // 结构变化不进撤销（决定 32）
    remerge();
    emit structureChanged();
    emit resultChanged();
}

void Session::removeRows(const QList<int>& rows)
{
    QList<int> sorted = rows;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int r : sorted) {
        if (r >= 0 && r < count())
            entries_.erase(entries_.begin() + r);
    }
    if (mpls_)
        analyzeMpls();
    else
        recomputePrefill();
    checkEpisodeOrder();
    undoStack_.clear();   // 结构变化不进撤销（决定 32）
    remerge();
    emit structureChanged();
    emit resultChanged();
}

void Session::moveRows(int srcRow, int count_, int destRow)
{
    if (srcRow < 0 || count_ <= 0 || srcRow + count_ > int(entries_.size()))
        return;
    if (destRow > int(entries_.size()))
        destRow = int(entries_.size());
    if (destRow >= srcRow && destRow <= srcRow + count_)
        return;   // 原地放下，无操作
    QList<Entry> moved;
    for (int i = 0; i < count_; ++i)
        moved.push_back(entries_[size_t(srcRow + i)]);
    entries_.erase(entries_.begin() + srcRow, entries_.begin() + srcRow + count_);
    const int insertAt = (destRow > srcRow) ? destRow - count_ : destRow;
    for (int i = 0; i < count_; ++i)
        entries_.insert(entries_.begin() + insertAt + i, std::move(moved[size_t(i)]));
    if (mpls_)
        analyzeMpls();
    else
        recomputePrefill();
    checkEpisodeOrder();
    undoStack_.clear();   // 结构变化不进撤销（决定 32）
    remerge();
    emit structureChanged();
    emit resultChanged();
}

void Session::checkEpisodeOrder()
{
    // T10：全部行集数可识别时核对顺序与重复（部分可识别则信息不足，不告警）。
    // 只告警不重排——顺序是用户的决定权（F16 冻结方案：建议不静默改序）。
    orderDiags_.clear();
    if (count() < 2)
        return;
    for (const Entry& e : entries_)
        if (!e.sub->epConfident)
            return;

    // T10：集数有洞（如 1、2、4）→ 提示可能缺集（多集一个 m2ts 场景缺一集
    // 就会整体错位，C1/C2 之外的前置提醒）
    int lo = entries_.front().sub->epNumber, hi = lo;
    for (const Entry& e : entries_) {
        lo = qMin(lo, e.sub->epNumber);
        hi = qMax(hi, e.sub->epNumber);
    }
    QString missing;
    for (int n = lo; n <= hi; ++n) {
        bool present = false;
        for (const Entry& e : entries_)
            present = present || e.sub->epNumber == n;
        if (!present)
            missing += (missing.isEmpty() ? QString() : QStringLiteral("、"))
                     + QString::number(n);
    }
    if (!missing.isEmpty())
        orderDiags_.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::EpisodeUnrecognized,
            QStringLiteral("集数不连续：缺第 %1 集（已载入第 %2~%3 集的"
                           "字幕）——多集一个 m2ts 时缺集会整体错位，请补齐"
                           "或确认").arg(missing).arg(lo).arg(hi)));

    for (int i = 1; i < count(); ++i) {
        const int prev = entries_[size_t(i - 1)].sub->epNumber;
        const int cur = entries_[size_t(i)].sub->epNumber;
        if (cur == prev) {
            orderDiags_.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::EpisodeUnrecognized,
                QStringLiteral("第 %1、%2 行都被识别为第 %3 集——请确认"
                               "是否重复载入了同一集")
                    .arg(i).arg(i + 1).arg(cur), i));
        } else if (cur < prev) {
            orderDiags_.push_back(Diagnostic::make(
                DiagLevel::Warning, DiagCode::EpisodeUnrecognized,
                QStringLiteral("列表顺序与集数不符：第 %1 行是第 %2 集，"
                               "排在第 %3 集之后——如非有意为之请拖动调整")
                    .arg(i + 1).arg(cur).arg(prev), i));
            break;   // 一次失序报一条即可，避免刷屏
        }
    }
}

void Session::setOffset(int row, Cs offset, bool fromTimeline)
{
    if (row < 0 || row >= count())
        return;
    pushUndo();
    Entry& e = entries_[size_t(row)];
    e.offset = offset;
    // 手改行不再被预填覆盖；时间轴拖动同样定案（依据可区分）
    e.source = fromTimeline ? EpisodeInput::OffsetSource::Timeline
                            : EpisodeInput::OffsetSource::Manual;
    recomputePrefill();   // 下游 Prefill 行连锁重算
    remerge();
    emit paramsChanged();
    emit resultChanged();
}

void Session::setGlobalShift(Cs shift)
{
    pushUndo();
    options_.globalShift = shift;
    remerge();
    emit paramsChanged();
    emit resultChanged();
}

void Session::recomputePrefill()
{
    // 预填连锁规则（§5.9）：第 N 集预填 = 前一集（偏移 + maxEnd）累加；
    // 只动"来源=Prefill"的行，Manual/Mpls/Adjusted 的行保持用户设定
    Cs running = 0;
    for (Entry& e : entries_) {
        if (e.source == EpisodeInput::OffsetSource::Prefill)
            e.offset = running;
        running = e.offset + e.doc->maxDialogueEnd;
    }
}

void Session::analyzeMpls()
{
    // v1.4.2（所有者裁定）：MPLS 只解析与建议，**不改偏移**——偏移默认恒为
    // 预填；建议值与依据列于告警面板，应用是显式动作（applyMplsSuggestion）。
    mplsDiags_.clear();
    chapterSuggestion_.reset();
    playItemSuggestion_.reset();
    mplsMainCount_ = 0;
    chapterMode_ = false;
    mplsClipName_.clear();

    // 结构性变化（增/删/移）后，此前显式应用过的建议已与列表脱节：
    // 回落预填并告警，不静默沿用可能错位的旧值
    bool hadApplied = false;
    for (Entry& e : entries_) {
        if (e.source == EpisodeInput::OffsetSource::Chapter ||
            e.source == EpisodeInput::OffsetSource::Mpls) {
            hadApplied = true;
            e.source = EpisodeInput::OffsetSource::Prefill;
            e.mplsEnd.reset();
        }
    }
    if (hadApplied) {
        recomputePrefill();
        mplsDiags_.push_back(Diagnostic::make(
            DiagLevel::Warning, DiagCode::MplsUnsupported,
            QStringLiteral("文件列表已变化，此前套用的原盘参考值已恢复为"
                           "自动推算——请重新核对或再次套用")));
    }

    if (!mpls_)
        return;
    std::vector<Diagnostic> diags;

    // 优先：章节建议（T8，v1.4 场景）——恰一个正片 PlayItem（多集一个 m2ts）
    QList<Cs> subEd;   // 每集字幕 ED 起点（T8 修正：ED 锚定）
    for (const Entry& e : entries_)
        subEd.append(detectEdStartCs(*e.doc));
    const ChapterMapping ch = suggestChapterStarts(*mpls_, count(), subEd, diags);
    if (ch.ok) {
        chapterMode_ = true;
        mplsMainCount_ = 1;
        mplsClipName_ = ch.clip;
        chapterSuggestion_ = ch;
        mplsDiags_.push_back(Diagnostic::make(
            DiagLevel::Info, DiagCode::MplsUnsupported,
            QStringLiteral("该 m2ts 含多集（%1）：已算出每集参考起点——自动"
                           "推算未改动，确认后点\"套用参考起点\"；导出文件名"
                           "建议与视频同名（%1.ass）").arg(ch.clip)));
        for (const Diagnostic& d : diags)
            mplsDiags_.push_back(d);
        // 预填交叉核对（T2 洞察的安全网）：连续切割的字幕自带分集信息
        // （黑契/死后文实测预填误差仅 0.05/2.3 秒）。章节建议与预填大幅
        // 背离 = 大概率建议错了——死后文实测 bug（建议吸到 OP 起点、偏晚
        // 27.6 秒）若有此检查当场拦截。
        if (count() > 1) {
            QList<std::shared_ptr<const AssDocument>> docs;
            docs.reserve(entries_.size());
            for (const Entry& e : entries_)
                docs.append(e.doc);
            const QList<Cs> prefill = computePrefillOffsets(docs);
            constexpr Cs kPrefillDivergeCs = 15 * 100;
            for (int k = 1; k < count(); ++k) {
                if (qAbs(ch.offsets[k] - prefill[k]) > kPrefillDivergeCs)
                    mplsDiags_.push_back(Diagnostic::make(
                        DiagLevel::Warning, DiagCode::MplsUnsupported,
                        QStringLiteral("第 %1 集参考起点 %2 与自动推算 %3 "
                                       "相差 %4 秒——两者应接近，套用前请逐集"
                                       "核对（自动推算 = 上一集字幕结束处）")
                            .arg(k + 1).arg(formatCs(ch.offsets[k]))
                            .arg(formatCs(prefill[k]))
                            .arg(qAbs(ch.offsets[k] - prefill[k]) / 100), k));
            }
        }
        return;
    }

    // 兜底：PlayItem 映射建议（一集一项的规整盘 / GTO 长链）
    MplsMapping m = mapItemsToEpisodes(*mpls_, count(), mplsStartMain_, diags);
    mplsMainCount_ = m.mainItems.size();
    for (const Diagnostic& d : diags)
        mplsDiags_.push_back(d);
    if (m.ok)
        playItemSuggestion_ = m;
}

bool Session::mplsSuggestionAvailable() const
{
    return chapterSuggestion_.has_value() || playItemSuggestion_.has_value();
}

QList<Cs> Session::mplsSuggestedOffsets() const
{
    if (chapterSuggestion_.has_value())
        return chapterSuggestion_->offsets;
    if (playItemSuggestion_.has_value())
        return playItemSuggestion_->offsets;
    return {};
}

bool Session::applyMplsSuggestion()
{
    if (chapterSuggestion_.has_value()) {
        pushUndo();
        const ChapterMapping& ch = *chapterSuggestion_;
        // 章节偏移下集间黑场/ED 空白是常态（死后文实测 1~2.5 分钟），
        // C2 空档阈值放宽到 5 分钟
        options_.boundaryGapThreshold = 5 * 6000;
        for (int i = 0; i < count(); ++i) {
            Entry& e = entries_[size_t(i)];
            e.offset = ch.offsets[i];
            e.mplsEnd = ch.episodeEnds[i];
            e.source = EpisodeInput::OffsetSource::Chapter;
        }
        mplsDiags_.push_back(Diagnostic::make(
            DiagLevel::Info, DiagCode::MplsUnsupported,
            QStringLiteral("已套用参考起点（依据列标\"原盘\"）——请逐集核对，"
                           "不对的可直接修改")));
        remerge();
        emit paramsChanged();
        emit resultChanged();
        return true;
    }
    if (playItemSuggestion_.has_value()) {
        pushUndo();
        const MplsMapping& m = *playItemSuggestion_;
        for (int i = 0; i < count(); ++i) {
            Entry& e = entries_[size_t(i)];
            e.offset = m.offsets[i];
            e.mplsEnd = m.episodeEnds[i];
            e.source = EpisodeInput::OffsetSource::Mpls;
        }
        mplsDiags_.push_back(Diagnostic::make(
            DiagLevel::Info, DiagCode::MplsUnsupported,
            QStringLiteral("已套用正片参考起点（依据列标\"原盘\"）——请逐集"
                           "核对")));
        remerge();
        emit paramsChanged();
        emit resultChanged();
        return true;
    }
    return false;
}

void Session::setSegmentPoints(const QList<SegmentPoint>& pts)
{
    pushUndo();
    options_.segmentPoints = pts;
    remerge();
    emit paramsChanged();
    emit resultChanged();
}

Session::ParamSnapshot Session::snapshotParams() const
{
    ParamSnapshot s;
    s.globalShift = options_.globalShift;
    s.gapThreshold = options_.boundaryGapThreshold;
    s.points = options_.segmentPoints;
    for (const Entry& e : entries_) {
        s.offsets.append(e.offset);
        s.sources.append(e.source);
        s.mplsEnds.append(e.mplsEnd);
    }
    return s;
}

void Session::pushUndo()
{
    // 参数快照式撤销（决定 32）：栈上限 50；结构变化时由调用方清栈
    undoStack_.append(snapshotParams());
    while (undoStack_.size() > 50)
        undoStack_.removeFirst();
}

void Session::undo()
{
    if (undoStack_.isEmpty())
        return;
    const ParamSnapshot s = undoStack_.takeLast();
    if (s.offsets.size() != count())   // 防御：结构与快照不符（不应发生）
        return;
    for (int i = 0; i < count(); ++i) {
        Entry& e = entries_[size_t(i)];
        e.offset = s.offsets[i];
        e.source = s.sources[i];
        e.mplsEnd = s.mplsEnds[i];
    }
    options_.segmentPoints = s.points;
    options_.globalShift = s.globalShift;
    options_.boundaryGapThreshold = s.gapThreshold;
    remerge();
    emit paramsChanged();
    emit resultChanged();
}

bool Session::loadMplsFile(const QString& path, QString* errMsg)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errMsg) *errMsg = file.errorString();
        return false;
    }
    const MplsOutcome out = parseMpls(file.readAll());
    if (!out.info.has_value()) {
        // 解析失败：降级为诊断（绝不给可疑偏移），保持现有偏移不动
        mplsDiags_ = QList<Diagnostic>(out.diags.begin(), out.diags.end());
        emit resultChanged();
        if (errMsg && !out.diags.empty())
            *errMsg = out.diags.front().message;
        return false;
    }
    mpls_ = *out.info;
    mplsStartMain_ = 0;   // 新文件从第 1 个正片开始
    mplsDiags_ = QList<Diagnostic>(out.diags.begin(), out.diags.end());
    analyzeMpls();
    remerge();
    emit structureChanged();
    emit resultChanged();
    return true;
}

void Session::clearMpls()
{
    mpls_.reset();
    mplsDiags_.clear();
    mplsMainCount_ = 0;
    mplsStartMain_ = 0;
    chapterMode_ = false;
    mplsClipName_.clear();
    chapterSuggestion_.reset();
    playItemSuggestion_.reset();
    options_.boundaryGapThreshold = 2 * 100;
    // 回落预填：全部行回到 Prefill 来源并连锁重算
    for (Entry& e : entries_) {
        e.source = EpisodeInput::OffsetSource::Prefill;
        e.mplsEnd.reset();
    }
    recomputePrefill();
    remerge();
    emit structureChanged();
    emit resultChanged();
}

void Session::setMplsStartMain(int startMain)
{
    if (startMain < 0 || startMain == mplsStartMain_ || !mpls_)
        return;
    mplsStartMain_ = startMain;
    analyzeMpls();
    remerge();
    emit structureChanged();
    emit resultChanged();
}

void Session::remerge()
{
    if (entries_.isEmpty()) {
        result_.reset();
        return;
    }
    QList<EpisodeInput> eps;
    eps.reserve(entries_.size());
    for (const Entry& e : entries_) {
        EpisodeInput ep;
        ep.doc = e.doc;
        ep.offset = e.offset;
        ep.fileId = e.sub->id;
        ep.mplsEnd = e.mplsEnd;
        ep.offsetSource = e.source;
        eps.push_back(std::move(ep));
    }
    result_ = merge(std::move(eps), options_);
}

QList<Diagnostic> Session::allDiags() const
{
    QList<Diagnostic> out = dropDiags_;
    out += orderDiags_;
    out += mplsDiags_;
    // 装载诊断补上文件在列表中的位置（装载时还不知道顺序）
    for (int i = 0; i < count(); ++i) {
        for (const Diagnostic& d : entries_[size_t(i)].sub->loadDiags) {
            Diagnostic dd = d;
            if (dd.fileIndex < 0)
                dd.fileIndex = i;
            out.push_back(dd);
        }
    }
    if (result_)
        for (const Diagnostic& d : result_->diags)
            out.push_back(d);
    return out;
}

int Session::errorCount() const
{
    int n = 0;
    for (const Diagnostic& d : allDiags())
        if (d.level == DiagLevel::Error)
            ++n;
    return n;
}

QString Session::suggestFileName() const
{
    // 章节模式（F07，v1.4）：与 m2ts 同名导出（00000.m2ts → 00000.ass，
    // 实证惯例：死后文盘 STREAM 内即有前任持有者放置的同名外挂）
    if (chapterMode_ && !mplsClipName_.isEmpty())
        return mplsClipName_ + QStringLiteral(".ass");

    // [01-04]_merged.ass（集数范围来自 epnum；识别不全时退化为文件数）
    int first = -1, last = -1;
    bool ok = !entries_.isEmpty();
    for (const Entry& e : entries_) {
        if (!e.sub->epConfident) { ok = false; break; }
        if (first < 0 || e.sub->epNumber < first) first = e.sub->epNumber;
        if (last < 0 || e.sub->epNumber > last) last = e.sub->epNumber;
    }
    if (!ok)
        return QStringLiteral("merged.ass");
    return QStringLiteral("[%1-%2]_merged.ass").arg(first, 2, 10, QChar(u'0'))
        .arg(last, 2, 10, QChar(u'0'));
}

bool Session::exportTo(const QString& path, QString* errMsg)
{
    if (!result_) {
        if (errMsg) *errMsg = QStringLiteral("没有可导出的合并结果");
        return false;
    }
    // 原子写（架构 §5.3）：QSaveFile 提交时才替换目标文件，崩溃不留半个文件
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errMsg) *errMsg = file.errorString();
        return false;
    }
    file.write(serializeAss(result_->merged));
    if (!file.commit()) {
        if (errMsg) *errMsg = file.errorString();
        return false;
    }
    return true;
}

}  // namespace suture
