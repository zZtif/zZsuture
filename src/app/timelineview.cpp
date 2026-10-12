#include "timelineview.hpp"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QWheelEvent>

#include <algorithm>

#include "time.hpp"

namespace suture {
namespace {

// 低饱和循环色（相邻集必然不同色，重叠时一眼分清谁压谁）
QColor rowColor(int i)
{
    static const QColor pal[] = {
        QColor(0x5b, 0x8d, 0xef), QColor(0x8f, 0x7e, 0xdb), QColor(0x5f, 0xae, 0x76),
        QColor(0xc9, 0x8a, 0x4b), QColor(0xc9, 0x6b, 0x8a), QColor(0x4f, 0xa8, 0xa0),
        QColor(0xa8, 0x9e, 0x4f), QColor(0x7a, 0x8f, 0xb5),
    };
    return pal[i % 8];
}

constexpr Cs kSnapTolCs = 30;                   // 吸附容差 0.3 秒（决定 28）
constexpr double kMinPxPerCs = 1.0 / 20000.0;   // 全览：2 小时占 ~360px
constexpr double kMaxPxPerCs = 2.0;             // 深放大：1px = 0.005s

// 主刻度步长（厘秒）：1s/2s/5s/10s/30s/1m/2m/5m/10m/30m/1h
constexpr Cs kMajorSteps[] = {
    100, 200, 500, 1000, 3000, 6000, 12000, 30000, 60000, 180000, 360000,
};

// 红色斜纹画刷（重叠带）：8px 斜线纹理，静态缓存
QBrush hatchBrush()
{
    static QBrush b;
    static bool inited = false;
    if (!inited) {
        QPixmap pm(8, 8);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setPen(QPen(QColor(0xc9, 0x50, 0x50, 0x90), 1));
        p.drawLine(0, 8, 8, 0);
        p.end();
        b = QBrush(pm);
        inited = true;
    }
    return b;
}

// 标尺标签：亚秒级主刻度用短格式（4:09.2），常规 H:MM:SS
QString rulerLabel(Cs t, Cs majorStep)
{
    if (majorStep < 100) {
        return QStringLiteral("%1:%2.%3")
            .arg(t / 100 / 60)
            .arg(t / 100 % 60, 2, 10, QChar(u'0'))
            .arg(int(t % 100) / 10);
    }
    return formatCs(t);
}

}  // namespace

TimelineView::TimelineView(Session* session, QWidget* parent)
    : QWidget(parent), s_(session)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(200);
    buildToolbar();
    // 会话变化 → 重算布局并重绘（拖动期间 Session 不发信号，预览安全）
    connect(s_, &Session::structureChanged, this, [this] { refresh(); update(); });
    connect(s_, &Session::paramsChanged, this, [this] { refresh(); update(); });
    connect(s_, &Session::resultChanged, this, [this] { refresh(); update(); });
    refresh();
}

void TimelineView::buildToolbar()
{
    // 工具条是子部件，绘制区从 kToolbarH 起（resize 时按钮由布局自适应）
    auto* bar = new QWidget(this);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(8, 4, 8, 4);
    auto* magnet = new QPushButton(tr("吸附"));
    magnet->setCheckable(true);
    magnet->setChecked(snapEnabled_);
    connect(magnet, &QPushButton::toggled, this,
            [this](bool on) { snapEnabled_ = on; });
    auto* zoomOut = new QPushButton(QStringLiteral("-"));
    auto* zoomIn = new QPushButton(QStringLiteral("+"));
    auto* fit = new QPushButton(tr("适配"));
    connect(zoomOut, &QPushButton::clicked, this,
            [this] { zoomAt(QPoint(width() / 2, 0), 1.0 / 1.25); });
    connect(zoomIn, &QPushButton::clicked, this,
            [this] { zoomAt(QPoint(width() / 2, 0), 1.25); });
    connect(fit, &QPushButton::clicked, this, [this] { fitToContent(); update(); });
    lay->addWidget(magnet);
    lay->addStretch(1);
    lay->addWidget(zoomOut);
    lay->addWidget(zoomIn);
    lay->addWidget(fit);
    // 工具条浮在顶部（绘制区从其下方开始）
    bar->setGeometry(0, 0, 400, kToolbarH);
    bar->setFixedHeight(kToolbarH);
    bar->raise();
}

// ---------------------------------------------------------------- 布局数据

void TimelineView::refresh()
{
    rows_.clear();
    points_.clear();
    chapterTicks_ = s_->chapterTicks();
    const Cs gs = s_->globalShift();

    const QList<SegmentPoint> allPts = s_->segmentPoints();
    for (int i = 0; i < s_->count(); ++i) {
        const Session::Entry& e = s_->entry(i);
        RowInfo r;
        r.row = i;
        r.fileId = e.sub->id;
        r.start = e.offset + gs;
        // 内容范围（fileTime 域）：最早的非 Comment 非零时长行
        Cs first = -1;
        for (const EventLine& ev : e.doc->events) {
            if (ev.isComment || ev.zeroDuration())
                continue;
            if (first < 0 || ev.start < first)
                first = ev.start;
        }
        r.contentFirst = first < 0 ? 0 : first;
        r.maxEnd = e.doc->maxDialogueEnd;
        r.color = rowColor(i);

        // 该文件的分段点按 at 升序；显示位置含前序累计增量
        QList<PointInfo> pis;
        Cs cum = 0;
        for (const SegmentPoint& sp : allPts) {
            if (sp.fileId != r.fileId)
                continue;
            PointInfo pi;
            pi.at = sp.at;
            pi.delta = sp.delta;
            pi.pos = r.start + sp.at + cum;
            cum += sp.delta;
            pis.append(pi);
        }
        std::sort(pis.begin(), pis.end(),
                  [](const PointInfo& a, const PointInfo& b) { return a.at < b.at; });
        r.totalWarp = cum;
        rows_.append(r);
        points_.append(pis);
    }

    // 防御：行数变化后选中/点选中越界即失效
    if (drag_ == Drag::None) {
        selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
                                        [this](int r) { return r >= rows_.size(); }),
                         selection_.end());
        if (selPointRow_ >= rows_.size() || selPointRow_ < 0) {
            selPointRow_ = -1;
            selPointIdx_ = -1;
        }
    }

    if (!fitted_ && !rows_.isEmpty()) {
        fitted_ = true;
        fitToContent();
    }
}

void TimelineView::fitToContent()
{
    Cs end = 0;
    for (const RowInfo& r : rows_)
        end = std::max(end, r.start + r.maxEnd + r.totalWarp);
    if (end <= 0)
        end = 600000;   // 空内容：默认 10 分钟视口
    viewLeft_ = -2000;  // 0 点墙留一点左边距
    pxPerCs_ = std::clamp(double(width()) / double(end + 4000),
                          kMinPxPerCs, kMaxPxPerCs);
    userAdjustedView_ = false;   // 适配后恢复"跟随窗口大小"行为
}

void TimelineView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    // 用户未手动缩放/平移时，窗口变化保持"适配"（工具条也随宽度铺开）
    if (!userAdjustedView_ && fitted_)
        fitToContent();
    if (auto* bar = findChild<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        bar->resize(width(), kToolbarH);
}

// ---------------------------------------------------------------- 取值（预览感知）

Cs TimelineView::rowFinalStart(int row) const
{
    if (drag_ == Drag::Block && previewStart_ >= 0 && dragRows_.contains(row)) {
        const Cs delta = previewStart_ - dragOrigStart_;
        return rows_[row].start + delta;
    }
    return rows_[row].start;
}

Cs TimelineView::rowFinalEnd(int row) const
{
    return rowFinalStart(row) + rows_[row].maxEnd + rows_[row].totalWarp;
}

QList<SegmentPoint> TimelineView::rowPoints(int row) const
{
    QList<SegmentPoint> out;
    for (const PointInfo& pi : points_[row]) {
        SegmentPoint sp;
        sp.fileId = rows_[row].fileId;
        sp.at = pi.at;
        sp.delta = pi.delta;
        out.append(sp);
    }
    // 点拖动预览：把被拖的点挪到预览位置（换算回 fileTime 域）
    if (drag_ == Drag::Point && dragRow_ == row && previewPointPos_ >= 0) {
        const Cs rowStart = rowFinalStart(row);
        Cs cum = 0;
        for (int k = 0; k < out.size(); ++k) {
            if (k == dragPointIdx_) {
                out[k].at = std::max(Cs(0), previewPointPos_ - rowStart - cum);
                break;
            }
            cum += out[k].delta;
        }
    }
    return out;
}

// ---------------------------------------------------------------- 命中测试

int TimelineView::hitPoint(const QPoint& pos) const
{
    if (std::abs(pos.y() - pointsY()) > 9)
        return -1;
    for (int r = 0; r < points_.size(); ++r)
        for (int k = 0; k < points_[r].size(); ++k)
            if (std::abs(timeToX(points_[r][k].pos) - pos.x()) <= 7)
                return r * 100000 + k;
    return -1;
}

int TimelineView::hitBlock(const QPoint& pos) const
{
    if (pos.y() < trackTop() || pos.y() > trackBottom())
        return -1;
    // 优先命中已选中的块（重叠时便于抓到下层），否则取包含 x 的第一个
    for (int r : selection_) {
        if (r >= rows_.size())
            continue;
        if (pos.x() >= timeToX(rowFinalStart(r)) && pos.x() <= timeToX(rowFinalEnd(r)))
            return r;
    }
    for (int r = 0; r < rows_.size(); ++r) {
        if (pos.x() >= timeToX(rowFinalStart(r)) && pos.x() <= timeToX(rowFinalEnd(r)))
            return r;
    }
    return -1;
}

// ---------------------------------------------------------------- 吸附

Cs TimelineView::snapCandidate(Cs t, int excludeRow) const
{
    // 优先级（决定 28）：章节刻度 > 其它块边缘 >（深放大）整秒刻度
    const auto nearest = [&t](const QList<Cs>& cands) -> Cs {
        Cs best = -1;
        for (Cs c : cands)
            if (c >= 0 && std::abs(c - t) <= kSnapTolCs
                && (best < 0 || std::abs(c - t) < std::abs(best - t)))
                best = c;
        return best;
    };
    Cs hit = nearest(chapterTicks_);
    if (hit >= 0)
        return hit;
    QList<Cs> edges;
    for (int r = 0; r < rows_.size(); ++r) {
        if (r == excludeRow)
            continue;
        edges.append(rowFinalStart(r));
        edges.append(rowFinalEnd(r));
    }
    hit = nearest(edges);
    if (hit >= 0)
        return hit;
    if (pxPerCs_ >= 0.3) {   // 深放大（1 秒 ≥ 30px）才吸整秒
        const Cs whole = (t / 100) * 100;
        if (std::abs(whole - t) <= kSnapTolCs)
            return whole;
    }
    return -1;
}

// ---------------------------------------------------------------- 绘制

void TimelineView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1a, 0x1c, 0x21));
    drawRuler(p);
    drawTrack(p);
    drawReadout(p);
}

void TimelineView::drawRuler(QPainter& p) const
{
    // 换挡：主刻度步长取"标签放得下"的最小档（间距 ≥ 64px）
    Cs major = kMajorSteps[int(std::size(kMajorSteps)) - 1];
    for (Cs s : kMajorSteps) {
        if (s * pxPerCs_ >= 64.0) {
            major = s;
            break;
        }
    }
    const Cs minor = major / 5;

    p.save();
    p.setClipRect(0, rulerTop(), width(), kRulerH);
    p.fillRect(0, rulerTop(), width(), kRulerH, QColor(0x1f, 0x21, 0x27));
    QFont f = p.font();
    f.setPointSizeF(9.0);   // 磅字号：随系统 DPI 缩放（需求 §5.3）
    p.setFont(f);

    const Cs t0 = std::max(Cs(0), (xToTime(0) / major) * major);
    for (Cs t = t0; timeToX(t) <= width(); t += major) {
        const int x = timeToX(t);
        // 主刻度网格线铺进轨道区（PR 式：拖块时视线不用抬头）
        p.setPen(QPen(QColor(0x2c, 0x2f, 0x38), 1));
        p.drawLine(x, rulerTop(), x, readoutTop());
        p.setPen(QPen(QColor(0x8b, 0x8e, 0x9a), 1));
        p.drawLine(x, rulerBottom() - 8, x, rulerBottom());
        p.drawText(QRect(x + 3, rulerTop() + 2, 96, 16), Qt::AlignLeft,
                   rulerLabel(t, major));
    }
    for (Cs t = t0 - t0 % minor; timeToX(t) <= width(); t += minor) {
        if (t % major == 0)
            continue;
        p.setPen(QPen(QColor(0x3a, 0x3d, 0x47), 1));
        p.drawLine(timeToX(t), rulerBottom() - 4, timeToX(t), rulerBottom());
    }
    // 章节刻度：强调色菱形 + 贯穿轨道的淡线
    for (Cs t : chapterTicks_) {
        const int x = timeToX(t);
        if (x < -10 || x > width() + 10)
            continue;
        p.setPen(QPen(QColor(0x4f, 0x8c, 0xff, 0x50), 1));
        p.drawLine(x, rulerBottom(), x, readoutTop());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x4f, 0x8c, 0xff));
        QPolygon dia;
        dia << QPoint(x, rulerBottom() - 9) << QPoint(x + 4, rulerBottom() - 5)
            << QPoint(x, rulerBottom() - 1) << QPoint(x - 4, rulerBottom() - 5);
        p.drawPolygon(dia);
    }
    p.setPen(QPen(QColor(0x33, 0x36, 0x3f), 1));
    p.drawLine(0, rulerBottom(), width(), rulerBottom());
    p.restore();
}

void TimelineView::drawTrack(QPainter& p) const
{
    p.save();
    p.setClipRect(0, rulerBottom(), width(), readoutTop() - rulerBottom());

    if (rows_.isEmpty()) {
        p.setPen(QColor(0x5a, 0x5d, 0x68));
        QFont f = p.font();
        f.setPointSize(12);
        p.setFont(f);
        p.drawText(rect().adjusted(0, trackTop(), 0, -kReadoutH), Qt::AlignCenter,
                   tr("拖入字幕文件后，这里显示每一集的时间块"));
        p.restore();
        return;
    }

    // x=0 硬墙（决定 36：ASS 时间非负，块不可拖过）
    const int x0 = timeToX(0);
    if (x0 >= -20 && x0 <= width() + 20) {
        p.setPen(QPen(QColor(0x5a, 0x5d, 0x68), 2, Qt::DashLine));
        p.drawLine(x0, rulerBottom(), x0, readoutTop());
        p.setPen(QColor(0x5a, 0x5d, 0x68));
        p.drawText(QRect(x0 + 4, trackTop(), 100, 14), Qt::AlignLeft,
                   tr("0 文件起点"));
    }

    // 重叠带：红色斜纹铺在轨道背景（两块边缘保持可见，不靠遮挡）
    for (int i = 0; i < rows_.size(); ++i) {
        for (int j = i + 1; j < rows_.size(); ++j) {
            const Cs lo = std::max(rowFinalStart(i), rowFinalStart(j));
            const Cs hi = std::min(rowFinalEnd(i), rowFinalEnd(j));
            if (hi <= lo)
                continue;
            p.fillRect(timeToX(lo), trackTop(),
                       timeToX(hi) - timeToX(lo), kTrackH, hatchBrush());
        }
    }

    // 集块 + 内容范围条
    for (int r = 0; r < rows_.size(); ++r) {
        const RowInfo& ri = rows_[r];
        const int x1 = timeToX(rowFinalStart(r));
        const int x2 = timeToX(rowFinalEnd(r));
        if (x2 < 0 || x1 > width())
            continue;
        const bool sel = selection_.contains(r);
        QColor fill = ri.color;
        fill.setAlpha(sel ? 110 : 70);
        QPainterPath path;
        path.addRoundedRect(QRectF(x1, trackTop(), x2 - x1, kTrackH), 7, 7);
        p.fillPath(path, fill);
        p.setPen(QPen(ri.color, sel ? 2 : 1));
        p.drawPath(path);
        // 内容范围条（对轴关键：该集字幕实际落在哪）
        Cs warpFirst = 0;
        for (const PointInfo& pi : points_[r])
            if (pi.at <= ri.contentFirst)
                warpFirst += pi.delta;
        const int cx1 = timeToX(rowFinalStart(r) + ri.contentFirst + warpFirst);
        if (x2 > cx1) {
            QColor c = ri.color.lighter(150);
            c.setAlpha(220);
            p.fillRect(cx1, trackTop() + kTrackH / 2 - 4, x2 - cx1, 8, c);
        }
        if (x2 - x1 > 34) {
            p.setPen(QColor(0xff, 0xff, 0xff, 220));
            p.drawText(QRect(x1 + 6, trackTop(), x2 - x1 - 8, kTrackH),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       tr("第%1集").arg(r + 1));
        }
    }

    // 分段点（菱形 + 与块的连线）
    for (int r = 0; r < points_.size(); ++r) {
        for (int k = 0; k < points_[r].size(); ++k) {
            const bool isDrag = drag_ == Drag::Point && dragRow_ == r
                                && dragPointIdx_ == k;
            const Cs pos = (isDrag && previewPointPos_ >= 0) ? previewPointPos_
                                                             : points_[r][k].pos;
            const int x = timeToX(pos);
            if (x < -10 || x > width() + 10)
                continue;
            const bool sel = selPointRow_ == r && selPointIdx_ == k;
            p.setPen(QPen(QColor(0x4f, 0x8c, 0xff, 120), 1));
            p.drawLine(x, trackBottom(), x, pointsY());
            p.setPen(QPen(sel ? QColor(0xff, 0xff, 0xff) : QColor(0x4f, 0x8c, 0xff),
                          sel ? 2 : 1));
            p.setBrush(isDrag ? QColor(0xff, 0xb0, 0x40) : QColor(0x4f, 0x8c, 0xff));
            QPolygon dia;
            dia << QPoint(x, pointsY() - 6) << QPoint(x + 5, pointsY())
                << QPoint(x, pointsY() + 6) << QPoint(x - 5, pointsY());
            p.drawPolygon(dia);
        }
    }

    // 吸附指示（决定 28：看得见的吸附才不是误操作）
    if (drag_ != Drag::None && snapTarget_ >= 0) {
        const int x = timeToX(snapTarget_);
        p.setPen(QPen(QColor(0xff, 0xd1, 0x64), 2));
        p.drawLine(x, rulerBottom(), x, readoutTop());
        p.drawText(QRect(x + 4, trackTop(), 200, 14), Qt::AlignLeft,
                   tr("吸附 %1").arg(formatCs(snapTarget_)));
    }

    // 框选矩形
    if (drag_ == Drag::Rubber) {
        p.setPen(QPen(QColor(0x4f, 0x8c, 0xff, 180), 1, Qt::DashLine));
        p.setBrush(QColor(0x4f, 0x8c, 0xff, 25));
        p.drawRect(rubber_.normalized());
    }
    p.restore();
}

void TimelineView::drawReadout(QPainter& p) const
{
    p.save();
    p.fillRect(0, readoutTop(), width(), kReadoutH, QColor(0x14, 0x15, 0x19));
    p.setPen(QPen(QColor(0x2c, 0x2f, 0x38), 1));
    p.drawLine(0, readoutTop(), width(), readoutTop());
    p.setPen(QColor(0x8b, 0x8e, 0x9a));
    QString left;
    if (rows_.isEmpty()) {
        left = tr("时间轴");
    } else if (drag_ == Drag::Block && previewStart_ >= 0) {
        const Cs delta = previewStart_ - dragOrigStart_;
        left = tr("第%1集：%2（Δ%3秒）")
                   .arg(dragRows_.isEmpty() ? 1 : dragRows_.first() + 1)
                   .arg(formatCs(previewStart_))
                   .arg(double(delta) / 100.0, 0, 'f', 1);
    } else if (!selection_.isEmpty()) {
        left = tr("选中 %1 集：第%2集 开始于 %3")
                   .arg(selection_.size())
                   .arg(selection_.first() + 1)
                   .arg(formatCs(rowFinalStart(selection_.first())));
    } else {
        left = tr("单击选块，拖动移动；右键加分段点；滚轮缩放");
    }
    const QString right = rows_.isEmpty()
        ? QString() : tr("光标 %1").arg(formatCs(xToTime(lastPos_.x())));
    p.drawText(QRect(10, readoutTop(), width() / 2 - 20, kReadoutH),
               Qt::AlignVCenter | Qt::AlignLeft, left);
    p.drawText(QRect(width() / 2, readoutTop(), width() / 2 - 10, kReadoutH),
               Qt::AlignVCenter | Qt::AlignRight, right);
    p.restore();
}

// ---------------------------------------------------------------- 鼠标

void TimelineView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    pressPos_ = e->pos();
    dragMoved_ = false;
    previewStart_ = -1;
    previewPointPos_ = -1;
    snapTarget_ = -1;

    if (const int h = hitPoint(e->pos()); h >= 0) {
        selPointRow_ = h / 100000;
        selPointIdx_ = h % 100000;
        selection_.clear();
        drag_ = Drag::Point;
        dragRow_ = selPointRow_;
        dragPointIdx_ = selPointIdx_;
        return;
    }
    selPointRow_ = selPointIdx_ = -1;

    if (const int r = hitBlock(e->pos()); r >= 0) {
        if (e->modifiers() & Qt::ControlModifier) {
            if (selection_.contains(r))
                selection_.removeOne(r);
            else
                selection_.append(r);
        } else if (!selection_.contains(r)) {
            selection_ = {r};
        }
        if (selection_.contains(r)) {
            drag_ = Drag::Block;
            dragRows_ = selection_;
            dragOrigStart_ = rows_[r].start;
            dragGrabOff_ = xToTime(e->pos().x()) - rows_[r].start;
        }
        update();
        return;
    }

    // 空白：框选（Ctrl 不清除已有选择）
    if (!(e->modifiers() & Qt::ControlModifier))
        selection_.clear();
    drag_ = Drag::Rubber;
    rubber_ = QRect(e->pos(), QSize(0, 0));
    update();
}

void TimelineView::mouseMoveEvent(QMouseEvent* e)
{
    lastPos_ = e->pos();
    if (drag_ == Drag::None) {
        setCursor(hitPoint(e->pos()) >= 0 || hitBlock(e->pos()) >= 0
                      ? Qt::SizeAllCursor : Qt::ArrowCursor);
        update();   // 读数条光标时刻
        return;
    }

    if (drag_ == Drag::Block) {
        Cs raw = xToTime(e->pos().x()) - dragGrabOff_;
        if (raw < 0)
            raw = 0;   // x=0 硬墙
        Cs target = raw;
        if (snapEnabled_ && !(e->modifiers() & Qt::AltModifier)) {
            const Cs hit = snapCandidate(raw, dragRows_.isEmpty() ? -1
                                                                  : dragRows_.first());
            if (hit >= 0)
                target = hit;
        }
        if (target < 0)
            target = 0;
        snapTarget_ = (target != raw) ? target : Cs(-1);
        previewStart_ = target;
        if (target != dragOrigStart_)
            dragMoved_ = true;
        update();
        return;
    }

    if (drag_ == Drag::Point) {
        Cs raw = xToTime(e->pos().x());
        const Cs rowStart = rowFinalStart(dragRow_);
        if (raw < rowStart)
            raw = rowStart;   // 点不越过本集起点
        Cs target = raw;
        if (snapEnabled_ && !(e->modifiers() & Qt::AltModifier)) {
            const Cs hit = snapCandidate(raw, dragRow_);
            if (hit >= 0 && hit >= rowStart)
                target = hit;
        }
        snapTarget_ = (target != raw) ? target : Cs(-1);
        previewPointPos_ = target;
        if (target != points_[dragRow_][dragPointIdx_].pos)
            dragMoved_ = true;
        update();
        return;
    }

    if (drag_ == Drag::Rubber) {
        rubber_ = QRect(pressPos_, e->pos()).normalized();
        QList<int> sel;
        if (rubber_.bottom() >= trackTop() && rubber_.top() <= trackBottom()) {
            for (int r = 0; r < rows_.size(); ++r) {
                const int x1 = timeToX(rowFinalStart(r));
                const int x2 = timeToX(rowFinalEnd(r));
                if (x2 >= rubber_.left() && x1 <= rubber_.right())
                    sel.append(r);
            }
        }
        selection_ = sel;
        update();
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (drag_ == Drag::Block && dragMoved_)
        commitBlockDrag();
    else if (drag_ == Drag::Point && dragMoved_)
        commitPointDrag();
    drag_ = Drag::None;
    dragRows_.clear();
    previewStart_ = -1;
    previewPointPos_ = -1;
    snapTarget_ = -1;
    rubber_ = QRect();
    update();
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (const int r = hitBlock(e->pos()); r >= 0) {
        selection_ = {r};
        openMoveEditor(r, QString());
    }
}

void TimelineView::wheelEvent(QWheelEvent* e)
{
    const QPoint deltas = e->angleDelta();
    if (e->modifiers() & Qt::ControlModifier) {
        userAdjustedView_ = true;
        viewLeft_ -= Cs(deltas.y() / 2.0 / pxPerCs_);   // Ctrl+滚轮：平移
    } else {
        zoomAt(e->position().toPoint(), deltas.y() > 0 ? 1.25 : 1.0 / 1.25);
    }
    update();
}

void TimelineView::zoomAt(const QPoint& center, double factor)
{
    userAdjustedView_ = true;   // 手动缩放后不再跟随窗口大小自动适配
    const Cs anchor = xToTime(center.x());
    pxPerCs_ = std::clamp(pxPerCs_ * factor, kMinPxPerCs, kMaxPxPerCs);
    viewLeft_ = anchor - Cs(center.x() / pxPerCs_);
    update();
}

// ---------------------------------------------------------------- 键盘

void TimelineView::keyPressEvent(QKeyEvent* e)
{
    if (moveEdit_ && moveEdit_->isVisible()) {
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const Cs step = (e->modifiers() & Qt::ShiftModifier) ? Cs(100) : Cs(10);
        nudgeSelection(e->key() == Qt::Key_Left ? -step : step);
        return;
    }
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelectedPoint();
        return;
    case Qt::Key_Escape:
        cancelDrag();
        return;
    case Qt::Key_Z:
        if (e->modifiers() & Qt::ControlModifier) {
            s_->undo();
            return;
        }
        break;
    case Qt::Key_F:
    case Qt::Key_Home:
        fitToContent();
        update();
        return;
    default:
        break;
    }
    // 选中块后直接敲数字 → 内联"移动到"（实测校准的最终形态，决定 33）
    if (!selection_.isEmpty() && e->text().size() == 1
        && (e->text().at(0).isDigit() || e->text().at(0) == u':')) {
        openMoveEditor(selection_.first(), e->text());
        return;
    }
    QWidget::keyPressEvent(e);
}

void TimelineView::nudgeSelection(Cs delta)
{
    if (selPointRow_ >= 0 && selPointIdx_ >= 0 && selPointRow_ < points_.size()
        && selPointIdx_ < points_[selPointRow_].size()) {
        // 键盘微调选中的分段点
        const quint64 fid = rows_[selPointRow_].fileId;
        QList<SegmentPoint> rowPts = rowPoints(selPointRow_);
        rowPts[selPointIdx_].at = std::max(Cs(0), rowPts[selPointIdx_].at + delta);
        QList<SegmentPoint> next;
        for (const SegmentPoint& sp : s_->segmentPoints())
            if (sp.fileId != fid)
                next.append(sp);
        next.append(rowPts);
        s_->setSegmentPoints(next);
        return;
    }
    if (selection_.isEmpty())
        return;
    QList<int> rows;
    QList<Cs> offs;
    const Cs gs = s_->globalShift();
    for (int r : selection_) {
        if (r < 0 || r >= rows_.size())
            continue;
        rows.append(r);
        offs.append(std::max(Cs(0), rows_[r].start + delta - gs));
    }
    if (!rows.isEmpty())
        s_->setOffsets(rows, offs, true);
}

void TimelineView::deleteSelectedPoint()
{
    if (selPointRow_ < 0 || selPointRow_ >= points_.size() || selPointIdx_ < 0
        || selPointIdx_ >= points_[selPointRow_].size())
        return;
    const quint64 fid = rows_[selPointRow_].fileId;
    QList<SegmentPoint> next;
    for (const SegmentPoint& sp : s_->segmentPoints())
        if (sp.fileId != fid)
            next.append(sp);
    s_->setSegmentPoints(next);
    selPointRow_ = selPointIdx_ = -1;
}

void TimelineView::addPointAt(int row, Cs finalPos)
{
    if (row < 0 || row >= rows_.size())
        return;
    SegmentPoint sp;
    sp.fileId = rows_[row].fileId;
    sp.at = std::max(Cs(0), finalPos - rowFinalStart(row));
    sp.delta = 0;
    QList<SegmentPoint> next = s_->segmentPoints();
    next.append(sp);
    s_->setSegmentPoints(next);
    // 选中新增点，便于接着拖动/微调（refresh 后下标对齐）
    selPointRow_ = row;
    selPointIdx_ = static_cast<int>(points_[row].size());
}

// ---------------------------------------------------------------- 提交

void TimelineView::commitBlockDrag()
{
    if (dragRows_.isEmpty() || previewStart_ < 0)
        return;
    const Cs delta = previewStart_ - dragOrigStart_;
    QList<int> rows;
    QList<Cs> offs;
    const Cs gs = s_->globalShift();
    for (int r : dragRows_) {
        if (r < 0 || r >= rows_.size())
            continue;
        rows.append(r);
        offs.append(std::max(Cs(0), rows_[r].start + delta - gs));
    }
    if (!rows.isEmpty())
        s_->setOffsets(rows, offs, true);   // 一次手势 = 一步撤销
}

void TimelineView::commitPointDrag()
{
    if (dragRow_ < 0 || dragRow_ >= rows_.size() || previewPointPos_ < 0)
        return;
    const quint64 fid = rows_[dragRow_].fileId;
    QList<SegmentPoint> next;
    for (const SegmentPoint& sp : s_->segmentPoints())
        if (sp.fileId != fid)
            next.append(sp);
    next.append(rowPoints(dragRow_));   // 含预览位置换算回 fileTime
    s_->setSegmentPoints(next);
}

void TimelineView::cancelDrag()
{
    if (moveEdit_ && moveEdit_->isVisible()) {
        moveEdit_->hide();
        return;
    }
    drag_ = Drag::None;
    previewStart_ = -1;
    previewPointPos_ = -1;
    snapTarget_ = -1;
    update();
}

// ---------------------------------------------------------------- 内联"移动到"

void TimelineView::ensureMoveEditor()
{
    if (moveEdit_)
        return;
    moveEdit_ = new QLineEdit(this);
    moveEdit_->setPlaceholderText(tr("0:23:46.00 回车确认，Esc 取消"));
    moveEdit_->hide();
    connect(moveEdit_, &QLineEdit::returnPressed, this, [this] {
        const auto cs = parseTimeToCs(moveEdit_->text());
        if (cs.has_value() && moveRow_ >= 0 && moveRow_ < rows_.size()) {
            const Cs gs = s_->globalShift();
            s_->setOffset(moveRow_, std::max(Cs(0), *cs - gs), true);
        }
        moveEdit_->hide();
    });
}

void TimelineView::openMoveEditor(int row, const QString& initial)
{
    ensureMoveEditor();
    moveRow_ = row;
    const int x = std::clamp(timeToX(rowFinalStart(row)) - 40, 4, width() - 230);
    moveEdit_->setGeometry(x, trackTop() + kTrackH + 2, 220, 24);
    moveEdit_->setText(initial);
    moveEdit_->setFocus();
    moveEdit_->show();
    moveEdit_->setSelection(0, moveEdit_->text().size());
}

// ---------------------------------------------------------------- 右键菜单

void TimelineView::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    if (const int h = hitPoint(e->pos()); h >= 0) {
        selPointRow_ = h / 100000;
        selPointIdx_ = h % 100000;
        menu.addAction(tr("删除分段点"), this, &TimelineView::deleteSelectedPoint);
    } else if (const int r = hitBlock(e->pos()); r >= 0) {
        const Cs t = xToTime(e->pos().x());
        menu.addAction(tr("在此处添加分段点"), this,
                       [this, r, t] { addPointAt(r, t); });
        menu.addAction(tr("移动到…"), this, [this, r] {
            selection_ = {r};
            openMoveEditor(r, QString());
        });
    } else {
        menu.addAction(tr("适配窗口"), this, [this] { fitToContent(); update(); });
    }
    menu.exec(e->globalPos());
}

}  // namespace suture
