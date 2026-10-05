// diagmodel.hpp —— 告警列表模型（等级/问题ID/文件/消息，架构 §5.9）
#pragma once

#include <QAbstractTableModel>

#include "diag.hpp"

namespace suture {

class DiagModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { ColLevel = 0, ColId, ColFile, ColMsg, ColCount };

    explicit DiagModel(QObject* parent = nullptr);

    void setDiags(const QList<Diagnostic>& diags);   // resultChanged 时整体刷新

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    QVariant headerData(int sec, Qt::Orientation o, int role) const override;

    const Diagnostic* at(int row) const;

private:
    QList<Diagnostic> diags_;
};

}  // namespace suture
