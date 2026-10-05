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

namespace suture {

class Session : public QObject {
    Q_OBJECT
public:
    struct Entry {
        std::shared_ptr<LoadedSubtitle> sub;   // 含稳定 ID/路径/装载诊断
        std::shared_ptr<const AssDocument> doc;
        Cs offset = 0;
        EpisodeInput::OffsetSource source = EpisodeInput::OffsetSource::Prefill;
    };

    explicit Session(QObject* parent = nullptr);

    // ---- 文件管理（结构性变化：触发预填重算 + 重合并）----
    void addUrls(const QList<QUrl>& urls);            // 拖放统一入口
    void removeRows(const QList<int>& rows);
    void moveRows(int srcRow, int count, int destRow);
    int  count() const { return int(entries_.size()); }
    const Entry& entry(int row) const { return entries_[size_t(row)]; }

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
    void remerge();

    QList<Entry> entries_;
    MergeOptions options_;
    quint64 nextId_ = 1;
    std::optional<MergeResult> result_;
    QList<Diagnostic> dropDiags_;   // expandDrop 的忽略/去重信息
};

}  // namespace suture
