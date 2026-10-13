// discaxisview.hpp —— A-r2（决定 43）：解析页纯数轴
// 与字幕轴（工作台，色块可拖）视觉彻底区分：本控件是"读数仪"——细线 +
// 刻度 + 语义标记，只读不可拖。可读性四层：形状分类 / EP 标签 / 图例 /
// 悬停详情（决定 43）。
//   ◆ 大菱形（强调色）= 建议分集点，旁标 EP2/EP3…
//   ▽ 小三角（黄）   = 疑似片尾曲起点（每集 ED 锚定实际所用章节）
//   │ 细刻度（灰）    = 全部章节
#pragma once

#include <QWidget>

#include "session.hpp"

namespace suture {

class DiscAxisView : public QWidget {
    Q_OBJECT
public:
    explicit DiscAxisView(Session* session, QWidget* parent = nullptr);

    // 会话变化后重取数据（章节/建议点/ED 锚定点/总时长）
    void refresh();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    // 悬停命中：返回 (kind, index)；kind: 0=章节 1=建议点 2=ED 锚定
    // 命中优先级：建议点 > ED 锚定 > 章节（重要的标记先命中）
    int hitMarker(int x) const;   // 返回 kind*100000+idx，-1 无
    int timeToX(Cs t) const;
    Cs xToTime(int x) const;

    Session* s_;
    QList<Cs> chapters_;      // 全部章节（排序去重）
    QList<Cs> suggests_;      // 建议分集点（每集）
    QList<Cs> eds_;           // ED 锚定章节（-1 无，按集对齐）
    QStringList basis_;       // 每集锚定依据（配合 suggests_ 悬停）
    Cs total_ = 0;            // 盘总时长（正片段长合计）
    int margin_ = 16;         // 左右留白（px）
};

}  // namespace suture
