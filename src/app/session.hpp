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

    // ---- MPLS（阶段 3 T1/T3 + 阶段 4 T8；v1.4.2 降级为"只建议"）----
    // 读文件 → 解析 → 计算建议（章节模式优先，PlayItem 映射兜底），但**不
    // 改偏移**——偏移默认恒为预填；建议值列于告警面板，由 applyMplsSuggestion
    // 显式应用（v1.4.2 原则：预测只建议、永不自动生效）。
    bool loadMplsFile(const QString& path, QString* errMsg);
    void clearMpls();
    bool hasMpls() const { return mpls_.has_value(); }
    const MplsInfo* mpls() const { return mpls_ ? &*mpls_ : nullptr; }
    int  mplsMainCount() const { return mplsMainCount_; }   // 正片项总数
    bool mplsChapterMode() const { return chapterMode_; }   // 解析出"多集一个 m2ts"结构
    QString mplsClipName() const { return mplsClipName_; }  // 多集 m2ts 的 clip 名（F07）
    bool mplsSuggestionAvailable() const;                   // 有可应用的建议？
    bool applyMplsSuggestion();                             // 显式应用（来源标"章节"/MPLS）
    // 字幕第 1 集对应第几个正片（0 基）——整季连播链只合并中间几集的场景
    // （章节模式下无效：分集点来自章节而非 PlayItem 边界）
    void setMplsStartMain(int startMain);

    // ---- T11 时间点累积校正 + 实测校准（v1.4.2 人工主线）----
    void setBreakpointShifts(const QList<BreakpointShift>& bps);
    QList<BreakpointShift> breakpointShifts() const { return options_.breakpointShifts; }
    // 各集字幕结束的基础时刻（推荐时间点；基础坐标 = 偏移+全局+该集 maxEnd，
    // 不含已填增量——对话框负责叠加显示）
    QList<Cs> episodeBoundaries() const;
    // 实测校准：observed = 该集首句内容在播放器中实际出现的时刻；
    // 新偏移 = observed − 该集最早内容时刻。返回新偏移。
    Cs calibrateToObserved(int row, Cs observedCs);
    Cs firstContentStart(int row) const;   // 该集最早非 Comment 非零时长行

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
    void analyzeMpls();        // v1.4.2：解析+算建议（不改偏移）；结构性变化时调用
    void checkEpisodeOrder();  // T10：列表顺序 vs 集数一致性（告警不静默改序）
    void remerge();

    QList<Entry> entries_;
    MergeOptions options_;
    quint64 nextId_ = 1;
    std::optional<MergeResult> result_;
    QList<Diagnostic> dropDiags_;   // expandDrop 的忽略/去重信息
    QList<Diagnostic> mplsDiags_;   // MPLS 解析/建议诊断
    QList<Diagnostic> orderDiags_;  // 顺序/集数一致性诊断（结构性变化时重算）
    std::optional<MplsInfo> mpls_;
    int mplsStartMain_ = 0;         // 字幕第 1 集对应的正片序号（0 基）
    int mplsMainCount_ = 0;
    bool chapterMode_ = false;      // T8：解析出"多集一个 m2ts"结构（信息性）
    QString mplsClipName_;          // 章节模式下多集 m2ts 的 clip 名
    std::optional<ChapterMapping> chapterSuggestion_;   // 待显式应用的建议
    std::optional<MplsMapping> playItemSuggestion_;     // PlayItem 兜底建议
};

}  // namespace suture
