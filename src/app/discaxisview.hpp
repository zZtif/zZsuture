// discaxisview.hpp —— A-r2（决定 43）：解析页纯数轴
// 与字幕轴（工作台，色块可拖）视觉彻底区分：本控件是"读数仪"——细线 +
// 刻度 + 语义标记，只读不可拖。可读性四层：形状分类 / EP 标签 / 图例 /
// 悬停详情（决定 43）。
//   ◆ 大菱形（强调色）= 建议分集点，旁标 EP2/EP3…
// "全部节点"开关已按验收反馈删除（节点拥挤且无助调整，整改文档 2.4）；
// 章节数据仅保留在 hover 之外不再展示——需要时看告警面板的依据文本。
#pragma once

#include <QWidget>

#include "session.hpp"

namespace suture {

class DiscAxisView : public QWidget {
    Q_OBJECT
public:
    explicit DiscAxisView(Session* session, QWidget* parent = nullptr);

    // 会话变化后重取数据（建议分集点/总时长/依据文本）
    void refresh();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    // 悬停命中建议分界点：返回索引，-1 无
    int hitMarker(int x) const;
    int timeToX(Cs t) const;
    Cs xToTime(int x) const;

    Session* s_;
    QList<Cs> suggests_;      // 建议分界点（每集；默认模式显示）
    QStringList basis_;       // 每集依据（悬停显示："链式预估 X → 吸附章节 Y"）
    Cs total_ = 0;            // 盘总时长
    int margin_ = 16;         // 左右留白（px）
};

}  // namespace suture
