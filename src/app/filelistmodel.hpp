// filelistmodel.hpp —— 文件列表表模型 + 拖放表格视图（架构 §5.9）
// 列：# / 文件名 / 集数 / 最晚End / 偏移(秒) / 来源；偏移列可编辑（F03=F06）。
// 行拖拽排序（F02）：纯鼠标事件实现（按下→移动超阈值→松开），不依赖
// Qt 的 DnD/InternalMove 机制——三次实测其行为不可控（clearOrRemove、
// 静默失效等），改为自绘插入指示线 + rowsDropped 信号，可用 QTest 模拟验证。
#pragma once

#include <QAbstractTableModel>
#include <QTableView>

#include "session.hpp"

namespace suture {

class FileListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { ColOrder = 0, ColName, ColEp, ColMaxEnd, ColOffset, ColSource, ColCount };

    explicit FileListModel(Session* session, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    bool setData(const QModelIndex& idx, const QVariant& v, int role) override;
    Qt::ItemFlags flags(const QModelIndex& idx) const override;
    QVariant headerData(int sec, Qt::Orientation o, int role) const override;

private:
    Session* s_;
};

// 鼠标拖拽排序表格：按住行 → 移动 → 目标位置松开；拖动中自绘插入指示线
class FileTableView : public QTableView {
    Q_OBJECT
public:
    using QTableView::QTableView;

signals:
    // 拖拽落定：把 srcRow 起的 count 行移到 destRow（与 Session::moveRows 同参）
    void rowsDropped(int srcRow, int count, int destRow);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    int insertRowAt(const QPoint& pos) const;   // 光标处的插入行号

    bool   dragArmed_ = false;   // 左键按在有效行上（等待移动超阈值）
    bool   dragging_ = false;    // 拖拽中
    int    pressRow_ = -1;
    QPoint pressPos_;
    int    hoverRow_ = -1;       // 拖动中的插入目标（画指示线用）
};

}  // namespace suture
