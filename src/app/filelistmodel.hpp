// filelistmodel.hpp —— 文件列表表模型（QAbstractTableModel，架构 §5.9）
// 列：# / 文件名 / 集数 / 最晚End / 偏移(秒) / 来源；偏移列可编辑（F03=F06）。
// 行拖拽排序（F02）经 mimeData/dropMimeData 走 Session::moveRows。
#pragma once

#include <QAbstractTableModel>

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

    // 拖拽排序
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& lists) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action,
                         int row, int col, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int col, const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;

private:
    Session* s_;
};

}  // namespace suture
