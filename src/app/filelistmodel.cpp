#include "filelistmodel.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>

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

// ---- FileTableView：鼠标拖拽排序 ----
// 纯鼠标事件实现：左键按在行上 → 移动超过 startDragDistance → 拖拽态；
// 移动中画插入指示线；松开时目标 ≠ 原位置 → rowsDropped → Session::moveRows。
// 不调用基类的拖放路径（QTableView::mouseMoveEvent 会触发橡皮筋选择，拖拽中跳过）。
void FileTableView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        pressPos_ = e->pos();
        pressRow_ = rowAt(pressPos_.y());
        dragArmed_ = pressRow_ >= 0;
    }
    QTableView::mousePressEvent(e);   // 选择等正常行为保留
}

void FileTableView::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton) {
        if (dragArmed_ && !dragging_ &&
            (e->pos() - pressPos_).manhattanLength() >=
                QApplication::startDragDistance()) {
            dragging_ = true;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        if (dragging_) {
            hoverRow_ = insertRowAt(e->pos());
            viewport()->update();
            return;   // 拖动中不走基类
        }
    }
    QTableView::mouseMoveEvent(e);
}

void FileTableView::mouseReleaseEvent(QMouseEvent* e)
{
    if (dragging_ && e->button() == Qt::LeftButton) {
        const int target = insertRowAt(e->pos());
        viewport()->unsetCursor();
        dragging_ = false;
        hoverRow_ = -1;
        viewport()->update();
        dragArmed_ = false;
        // 目标 == 自身或自身后一行 = 原地放下，无操作
        if (pressRow_ >= 0 && target != pressRow_ && target != pressRow_ + 1)
            emit rowsDropped(pressRow_, 1, target);
        pressRow_ = -1;
        return;
    }
    dragArmed_ = false;
    QTableView::mouseReleaseEvent(e);
}

int FileTableView::insertRowAt(const QPoint& pos) const
{
    const int r = rowAt(pos.y());
    if (r < 0)
        return model() ? model()->rowCount() : 0;
    // 光标在行下半部 → 插到该行之后
    return pos.y() > visualRect(model()->index(r, 0)).center().y() ? r + 1 : r;
}

void FileTableView::paintEvent(QPaintEvent* e)
{
    QTableView::paintEvent(e);
    if (!dragging_ || hoverRow_ < 0 || !model() || model()->rowCount() == 0)
        return;
    QPainter p(viewport());
    p.setPen(QPen(QColor(0x4F, 0x8C, 0xFF), 3));
    const int rows = model()->rowCount();
    const int y = (hoverRow_ >= rows)
        ? visualRect(model()->index(rows - 1, 0)).bottom()   // 追加到末尾
        : visualRect(model()->index(hoverRow_, 0)).top();
    p.drawLine(0, y, viewport()->width(), y);
}

}  // namespace suture
