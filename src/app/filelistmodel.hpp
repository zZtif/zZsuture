// filelistmodel.hpp —— 文件列表表模型 + 拖放表格视图（架构 §5.9）
// 列：# / 文件名 / 集数 / 最晚End / 偏移(秒) / 来源；偏移列可编辑（F03=F06）。
// 行拖拽排序（F02）：视图完全接管 dropEvent（绕开 Qt InternalMove 的
// 隐藏行为——clearOrRemove 会在拖放后删源行等），行为确定可控。
#pragma once

#include <QAbstractTableModel>
#include <QTableView>

#include "session.hpp"

namespace suture {

// 行拖拽的 mime 类型（模型编码 / 视图解码共用）
inline constexpr char kRowMime[] = "application/x-suture-rows";

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

    // 拖拽：模型负责编码行号（mimeData）与格式应答；落点处理在 FileTableView
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& lists) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action,
                         int row, int col, const QModelIndex& parent) const override;
    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;

private:
    Session* s_;
};

// 确定性拖放表格：dropEvent 自行解码行号并上报，不依赖 Qt 的 InternalMove
class FileTableView : public QTableView {
    Q_OBJECT
public:
    using QTableView::QTableView;

signals:
    // 拖拽落定：把 srcRow 起的 count 行移到 destRow（与 Session::moveRows 同参）
    void rowsDropped(int srcRow, int count, int destRow);

protected:
    void dropEvent(QDropEvent* e) override;
};

}  // namespace suture
