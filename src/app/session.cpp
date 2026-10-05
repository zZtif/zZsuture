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
        applyMplsMapping();
    else
        recomputePrefill();
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
        applyMplsMapping();
    else
        recomputePrefill();
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
        applyMplsMapping();
    else
        recomputePrefill();
    remerge();
    emit structureChanged();
    emit resultChanged();
}

void Session::setOffset(int row, Cs offset)
{
    if (row < 0 || row >= count())
        return;
    Entry& e = entries_[size_t(row)];
    e.offset = offset;
    e.source = EpisodeInput::OffsetSource::Manual;   // 手改行不再被预填覆盖
    recomputePrefill();   // 下游 Prefill 行连锁重算
    remerge();
    emit paramsChanged();
    emit resultChanged();
}

void Session::setGlobalShift(Cs shift)
{
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

void Session::applyMplsMapping()
{
    // MPLS 生效期间全部行来源切换为 MPLS（架构 §5.9：拖入 MPLS 后覆盖预填）。
    // 超出正片总数时 mapItemsToEpisodes 发告警并保持现有偏移（手动兜底）。
    mplsDiags_.clear();
    mplsMainCount_ = 0;
    if (!mpls_)
        return;
    std::vector<Diagnostic> diags;
    MplsMapping m = mapItemsToEpisodes(*mpls_, count(), mplsStartMain_, diags);
    mplsMainCount_ = m.mainItems.size();
    for (const Diagnostic& d : diags)
        mplsDiags_.push_back(d);
    if (!m.ok)
        return;
    for (int i = 0; i < count(); ++i) {
        Entry& e = entries_[size_t(i)];
        e.offset = m.offsets[i];
        e.mplsEnd = m.episodeEnds[i];
        e.source = EpisodeInput::OffsetSource::Mpls;
    }
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
    applyMplsMapping();
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
    applyMplsMapping();
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
