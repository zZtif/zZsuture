#include "filelistmodel.hpp"

#include <QFileInfo>
#include <QMimeData>

#include "time.hpp"

namespace suture {
namespace {
const char* kMimeRows = "application/x-suture-rows";
}

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

// ---- 拖拽排序：编码行号 → Session::moveRows ----

QStringList FileListModel::mimeTypes() const
{
    return {QString::fromLatin1(kMimeRows), QStringLiteral("text/plain")};
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
    m->setData(QString::fromLatin1(kMimeRows), bytes);
    return m;
}

bool FileListModel::canDropMimeData(const QMimeData* data, Qt::DropAction,
                                    int, int, const QModelIndex& parent) const
{
    return data->hasFormat(QString::fromLatin1(kMimeRows)) && !parent.isValid();
}

bool FileListModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
                                 int row, int, const QModelIndex& parent)
{
    if (action == Qt::IgnoreAction || !canDropMimeData(data, action, row, 0, parent))
        return false;
    if (row < 0 || row > s_->count())
        row = s_->count();
    QByteArray bytes = data->data(QString::fromLatin1(kMimeRows));
    QDataStream ds(&bytes, QIODevice::ReadOnly);
    QList<int> rows;
    ds >> rows;
    if (rows.isEmpty())
        return false;
    s_->moveRows(rows.first(), rows.size(), row);
    return true;
}

Qt::DropActions FileListModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

}  // namespace suture
