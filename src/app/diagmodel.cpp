#include "diagmodel.hpp"

#include <QColor>

namespace suture {

DiagModel::DiagModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void DiagModel::setDiags(const QList<Diagnostic>& diags)
{
    beginResetModel();
    diags_ = diags;
    endResetModel();
}

int DiagModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(diags_.size());
}

int DiagModel::columnCount(const QModelIndex&) const
{
    return ColCount;
}

static QString levelName(DiagLevel l)
{
    switch (l) {
    case DiagLevel::Error:   return QStringLiteral("● 错误");
    case DiagLevel::Warning: return QStringLiteral("⚠ 警告");
    default:                 return QStringLiteral("ℹ 提示");
    }
}

QVariant DiagModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || idx.row() >= int(diags_.size()))
        return {};
    const Diagnostic& d = diags_[idx.row()];

    if (role == Qt::ForegroundRole) {
        // 等级色：错误红 / 警告黄 / 提示灰（配合暗色主题）
        switch (d.level) {
        case DiagLevel::Error:   return QColor(0xE0, 0x6C, 0x6C);
        case DiagLevel::Warning: return QColor(0xE0, 0xC0, 0x6C);
        default:                 return QColor(0x8B, 0x8E, 0x9A);
        }
    }
    if (role != Qt::DisplayRole)
        return {};
    switch (idx.column()) {
    case ColLevel: return levelName(d.level);
    case ColId:    return QString::fromLatin1(diagCodeId(d.code));
    case ColFile:  return d.fileIndex >= 0
                       ? QVariant(QString::number(d.fileIndex + 1))
                       : QVariant(QStringLiteral("—"));
    case ColMsg:   return d.message;
    }
    return {};
}

QVariant DiagModel::headerData(int sec, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (sec) {
    case ColLevel: return QStringLiteral("等级");
    case ColId:    return QStringLiteral("ID");
    case ColFile:  return QStringLiteral("文件#");
    case ColMsg:   return QStringLiteral("消息");
    }
    return {};
}

const Diagnostic* DiagModel::at(int row) const
{
    return (row >= 0 && row < int(diags_.size())) ? &diags_[size_t(row)] : nullptr;
}

}  // namespace suture
