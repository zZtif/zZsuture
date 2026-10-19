// timelineview.hpp —— F08 时间轴主视图（v2.0 核心交互，需求 §4/F08）
// 自定义控件（不用 QGraphicsView/QML，理由见需求 §7）：交互纯一维，所有
// 编辑经 Session 单一事实源（拖块 = setOffsets(fromTimeline)，依据标"时间轴"）。
// 语义要点：
//   · 涟漪式拖动——拖第 N 集块其后所有集同 Δ 平移（决定 45），Alt 单块；
//   · 吸附已删除（验收反馈 2.5：实测没用）——拖动即裸平移；
//   · 台词时间常显：右键"显示台词时间"（悬停不稳定已删，反馈 2.2），
//     时刻实时同步块位置（反馈 2.3）；
//   · x=0 硬墙（ASS 时间非负）；重叠允许、红色斜纹标示；
//   · 拖动中本地预览，松手才提交 Session（合并重算是整次操作，不逐像素）；
//   · Ctrl+Z = Session 参数快照撤销。
#pragma once

#include <QLineEdit>
#include <QWidget>

#include "session.hpp"

namespace suture {

class TimelineView : public QWidget {
    Q_OBJECT
public:
    explicit TimelineView(Session* session, QWidget* parent = nullptr);

    // 会话结构/参数变化后调用：重算行布局数据（内容范围/分段点显示位置）。
    // 视口不重置——拖动期间 Session 不发信号，预览不受影响
    void refresh();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    // ---- 布局数据（refresh 重算；时间坐标 = 合并时间轴最终时刻，厘秒）----
    struct RowInfo {
        int row = 0;
        int epNumber = -1;    // 实际集数（文件名/内容识别；-1 = 未识别，
                             // 显示回退用行序号——验收反馈：拖入 6-10 集
                             // 时块上不能还写"第1集第2集"）
        quint64 fileId = 0;
        Cs start = 0;         // 块起点（offset + globalShift，最终坐标）
        Cs contentFirst = 0;  // 该集最早内容（fileTime 域）
        Cs maxEnd = 0;        // 该集最晚结束（fileTime 域）
        Cs totalWarp = 0;     // 该集全部分段点增量之和（画块尾用）
        QString firstText;    // 首句台词（去特效标签，截断；悬停显示）
        QString lastText;     // 末句台词（结束最晚那行）
        Cs lastStart = 0;     // 末句那行的开始（fileTime 域）
        QColor color;
    };
    struct PointInfo {
        Cs pos = 0;      // 最终坐标显示位置（start + at + 前序点累计增量）
        Cs at = 0;       // fileTime 域原始位置
        Cs delta = 0;
    };
    QList<RowInfo> rows_;
    QList<QList<PointInfo>> points_;   // 分段点数据（UI 已移除，决定 45；
                                        // 保留供 totalWarp 计算，恒为空）
    QList<Cs> chapterTicks_;
    // A-r2（决定 44）：盘标记叠加层——默认不显示（"蓝点太多"反馈），
    // 解析页"显示映射"开关控制
    QList<Cs> discSuggests_;   // 建议分集点（叠加时蓝色菱形 + EP 标签）
    bool discOverlay_ = false;

public:
    void setDiscOverlay(bool on) { discOverlay_ = on; update(); }

    // ---- 视口（时间 ↔ 像素）----
    double pxPerCs_ = 0.02;   // 约 1px=0.5s；首个内容 refresh 时适配
    Cs viewLeft_ = 0;         // 视口左缘的最终时刻
    bool fitted_ = false;     // 只在首次有内容时自动适配，此后尊重用户缩放
    bool userAdjustedView_ = false;   // 用户手动缩放/平移后，resize 不再自动适配
    int timeToX(Cs t) const { return int((t - viewLeft_) * pxPerCs_); }
    Cs xToTime(int x) const { return viewLeft_ + Cs(x / pxPerCs_); }

    // ---- 几何分区（px）----
    static constexpr int kToolbarH = 34;
    static constexpr int kRulerH = 30;
    static constexpr int kReadoutH = 24;
    static constexpr int kTrackH = 46;
    int rulerTop() const { return kToolbarH; }
    int rulerBottom() const { return kToolbarH + kRulerH; }
    int trackTop() const { return rulerBottom() + 6; }
    int trackBottom() const { return trackTop() + kTrackH; }
    int pointsY() const { return trackBottom() + 16; }
    int readoutTop() const { return height() - kReadoutH; }

    // ---- 命中测试：块 → row；-1 无 ----
    int hitBlock(const QPoint& pos) const;

    // ---- 拖动状态 ----
    enum class Drag { None, Block, Rubber, Pan };
    Drag drag_ = Drag::None;
    QPoint pressPos_;
    QPoint lastPos_;               // 读数条光标时刻（setMouseTracking）
    Cs panGrabTime_ = 0;           // 平移起点：按住时的光标时刻（抓着走）
    Cs panGrabViewLeft_ = 0;
    QList<int> dragRows_;          // 块拖动影响的行（多选批量）
    Cs dragGrabOff_ = 0;           // 按下点相对块起点的偏移
    Cs dragOrigStart_ = 0;         // 起始块原起点（算 Δ 用）
    bool dragMoved_ = false;
    Cs previewStart_ = -1;         // 块拖动预览：主块新起点（-1 无预览）
    QRect rubber_;                 // 框选矩形（widget 坐标）
    QList<int> selection_;         // 选中的行
    int scriptRow_ = -1;           // 右键"显示台词时间"常显的行（-1 无）

    // ---- 内联"移动到"（选中块后敲数字 / 双击块）----
    QLineEdit* moveEdit_ = nullptr;
    int moveRow_ = -1;
    void openMoveEditor(int row, const QString& initial);
    void ensureMoveEditor();

    // ---- 绘制 ----
    void drawRuler(QPainter& p) const;
    void drawTrack(QPainter& p) const;
    void drawReadout(QPainter& p) const;

    // ---- 预览感知取值（拖动中返回预览值）----
    Cs rowFinalStart(int row) const;
    Cs rowFinalEnd(int row) const;     // 块尾 = 起点 + maxEnd + totalWarp

    // ---- 操作 ----
    void commitBlockDrag(bool solo);   // solo = Alt+拖（只动该块，决定 45）
    void cancelDrag();
    void nudgeSelection(Cs delta, bool solo);
    void removeSelectedEpisodes();     // Delete：移除选中的整集（决定 45）
    void zoomAt(const QPoint& center, double factor);
    void fitToContent();
    void buildToolbar();
    void showHelp();               // ❕：全部操作方式速查表（决定 48）

    Session* s_;
};

}  // namespace suture
