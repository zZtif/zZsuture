// session.hpp —— app 层唯一状态源（架构 §5.9）
// 声明式模型（D7）：文件列表 + 参数（偏移/微调）是状态，合并结果是缓存——
// 任何参数变化 → 重算 merge（<100ms），无撤销栈、无可变结果文档。
#pragma once

#include <QList>
#include <QObject>
#include <QUrl>
#include <memory>
#include <optional>

#include "assdoc.hpp"
#include "load.hpp"
#include "merge.hpp"
#include "mpls.hpp"

namespace suture {

class Session : public QObject {
    Q_OBJECT
public:
    struct Entry {
        std::shared_ptr<LoadedSubtitle> sub;   // 含稳定 ID/路径/装载诊断
        std::shared_ptr<const AssDocument> doc;
        Cs offset = 0;
        std::optional<Cs> mplsEnd;             // MPLS 出点（C1 交叉验证用）
        EpisodeInput::OffsetSource source = EpisodeInput::OffsetSource::Prefill;
    };

    explicit Session(QObject* parent = nullptr);

    // ---- 文件管理（结构性变化：触发预填重算 + 重合并）----
    void addUrls(const QList<QUrl>& urls);            // 拖放统一入口
    void removeRows(const QList<int>& rows);
    void moveRows(int srcRow, int count, int destRow);
    int  count() const { return int(entries_.size()); }
    const Entry& entry(int row) const { return entries_[size_t(row)]; }

    // ---- MPLS（阶段 3 T1/T3 + 阶段 4 T8 章节模式）----
    // 读文件 → 解析 → 映射到每集。优先尝试章节模式（v1.4 场景：恰一个正片
    // PlayItem = 多集一个 m2ts，按章节建议分集点，来源标"章节"）；不适用则
    // 回落 PlayItem 映射（D10：短项过滤 + 起始正片对齐，来源标"MPLS"）。
    bool loadMplsFile(const QString& path, QString* errMsg);
    void clearMpls();
    bool hasMpls() const { return mpls_.has_value(); }
    const MplsInfo* mpls() const { return mpls_ ? &*mpls_ : nullptr; }
    int  mplsMainCount() const { return mplsMainCount_; }   // 正片项总数
    bool mplsChapterMode() const { return chapterMode_; }   // 章节模式生效中
    QString mplsClipName() const { return mplsClipName_; }  // 多集 m2ts 的 clip 名（F07）
    // 字幕第 1 集对应第几个正片（0 基）——整季连播链只合并中间几集的场景
    // （章节模式下无效：分集点来自章节而非 PlayItem 边界）
    void setMplsStartMain(int startMain);

    // ---- 参数（F03 设定与 F06 微调是同一入口）----
    void setOffset(int row, Cs offset);               // 手改 → 来源变 Manual
    void setGlobalShift(Cs shift);

    // ---- 结果 ----
    const MergeResult* result() const { return result_ ? &*result_ : nullptr; }
    QList<Diagnostic>  allDiags() const;              // 装载 + 合并全部诊断
    int errorCount() const;

    QString suggestFileName() const;                  // [01-04]_merged.ass
    bool exportTo(const QString& path, QString* errMsg);   // QSaveFile 原子写

signals:
    void structureChanged();   // 增/删/移（模型 reset + 预填重算）
    void paramsChanged();      // 偏移/微调变化
    void resultChanged();      // 合并结果刷新（诊断/导出可用性）

private:
    void recomputePrefill();   // 只回填"来源=Prefill 且未被手改"的行（§5.9）
    void applyMplsMapping();   // mpls_ 存在时按 D10 重映射全部行
    void remerge();

    QList<Entry> entries_;
    MergeOptions options_;
    quint64 nextId_ = 1;
    std::optional<MergeResult> result_;
    QList<Diagnostic> dropDiags_;   // expandDrop 的忽略/去重信息
    QList<Diagnostic> mplsDiags_;   // MPLS 解析/映射诊断
    std::optional<MplsInfo> mpls_;
    int mplsStartMain_ = 0;         // 字幕第 1 集对应的正片序号（0 基）
    int mplsMainCount_ = 0;
    bool chapterMode_ = false;      // T8：章节分集模式生效中
    QString mplsClipName_;          // 章节模式下多集 m2ts 的 clip 名
};

}  // namespace suture
