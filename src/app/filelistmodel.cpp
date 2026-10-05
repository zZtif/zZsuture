#include "filelistmodel.hpp"

#include <QDataStream>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>

#include <algorithm>

#include "time.hpp"

namespace suture {

FileListModel::FileListModel(Session* session, QObject* parent)
    : QAbstractTableModel(parent), s_(session)
{
    connect(session, &Session::structureChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
    connect(session, &Session::paramsChanged, this, [this] {
        emit dataChanged(index(0, ColOffset), index(rowCount() - 1, ColSource));
    });
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : s_->count();
}

int FileListModel::columnCount(const QModelIndex&) const
{
    return ColCount;
}

QVariant FileListModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || idx.row() >= s_->count())
        return {};
    const Session::Entry& e = s_->entry(idx.row());

    if (role == Qt::TextAlignmentRole &&
        (idx.column() == ColOrder || idx.column() == ColEp ||
         idx.column() == ColMaxEnd || idx.column() == ColOffset))
        return int(Qt::AlignRight | Qt::AlignVCenter);

    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    switch (idx.column()) {
    case ColOrder:
        return idx.row() + 1;
    case ColName:
        return QFileInfo(e.sub->path).fileName();
    case ColEp:
        return e.sub->epConfident
            ? QVariant(QString::number(e.sub->epNumber))
            : QVariant(QStringLiteral("？"));
    case ColMaxEnd:
        return formatCs(e.doc->maxDialogueEnd);
    case ColOffset:
        // 偏移列以秒显示（用户直觉单位），内部厘秒
        return double(e.offset) / 100.0;
    case ColSource:
        switch (e.source) {
        case EpisodeInput::OffsetSource::Mpls:    return QStringLiteral("MPLS");
        case EpisodeInput::OffsetSource::Manual:  return QStringLiteral("手动");
        case EpisodeInput::OffsetSource::Adjusted:return QStringLiteral("调整");
        default:                                  return QStringLiteral("预填");
        }
    }
    return {};
}

bool FileListModel::setData(const QModelIndex& idx, const QVariant& v, int role)
{
    if (!idx.isValid() || role != Qt::EditRole || idx.column() != ColOffset)
        return false;
    bool ok = false;
    const double sec = v.toDouble(&ok);
    if (!ok)
        return false;
    s_->setOffset(idx.row(), Cs(std::llround(sec * 100.0)));   // 秒 → 厘秒
    return true;
}

Qt::ItemFlags FileListModel::flags(const QModelIndex& idx) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(idx) | Qt::ItemIsDragEnabled;
    if (idx.column() == ColOffset)
        f |= Qt::ItemIsEditable;
    return f;
}

QVariant FileListModel::headerData(int sec, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (sec) {
    case ColOrder:   return QStringLiteral("#");
    case ColName:    return QStringLiteral("文件名");
    case ColEp:      return QStringLiteral("集");
    case ColMaxEnd:  return QStringLiteral("最晚结束");
    case ColOffset:  return QStringLiteral("偏移(秒)");
    case ColSource:  return QStringLiteral("来源");
    }
    return {};
}

// ---- 拖拽排序：模型编码行号；落点由 FileTableView::dropEvent 决定 ----

QStringList FileListModel::mimeTypes() const
{
    return {QString::fromLatin1(kRowMime)};
}

QMimeData* FileListModel::mimeData(const QModelIndexList& lists) const
{
    QList<int> rows;
    for (const QModelIndex& i : lists)
        if (!rows.contains(i.row()))
            rows.append(i.row());
    std::sort(rows.begin(), rows.end());
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds << rows;
    auto* m = new QMimeData;
    m->setData(QString::fromLatin1(kRowMime), bytes);
    return m;
}

bool FileListModel::canDropMimeData(const QMimeData* data, Qt::DropAction,
                                    int, int, const QModelIndex& parent) const
{
    return data->hasFormat(QString::fromLatin1(kRowMime)) && !parent.isValid();
}

Qt::DropActions FileListModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

Qt::DropActions FileListModel::supportedDragActions() const
{
    // 不覆写此项时基类返回 CopyAction，拖拽会静默失效（验证反馈实锤）
    return Qt::MoveAction;
}

// ---- FileTableView：确定性拖放 ----
// 完全接管 dropEvent，不走 QAbstractItemView 默认 InternalMove 路径
// （默认路径在成功后会对源行调 clearOrRemove，与我们的 moveRows 语义冲突）。
void FileTableView::dropEvent(QDropEvent* e)
{
    if (e->source() != this || !e->mimeData()->hasFormat(QString::fromLatin1(kRowMime))) {
        QTableView::dropEvent(e);   // 非本表行拖拽（理论上不会发生）
        return;
    }
    // 落点行：鼠标所在行；指示器在其下半部 → 插到该行之后；空白区 → 追加
    const QModelIndex idx = indexAt(e->position().toPoint());
    int row = idx.isValid() ? idx.row() : model()->rowCount();
    if (idx.isValid() && dropIndicatorPosition() == QAbstractItemView::BelowItem)
        ++row;

    QByteArray bytes = e->mimeData()->data(QString::fromLatin1(kRowMime));
    QDataStream ds(&bytes, QIODevice::ReadOnly);
    QList<int> rows;
    ds >> rows;
    if (rows.isEmpty())
        return;
    e->acceptProposedAction();
    emit rowsDropped(rows.first(), rows.size(), row);
}

}  // namespace suture
