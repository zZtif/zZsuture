#include "timelineview.hpp"

#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
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

// 台词文本清洗：去掉 {\...} 特效标签、压缩空白、截断 24 字（悬停显示用）
QString cleanLine(QString t)
{
    static const QRegularExpression tags(QStringLiteral("\\{[^}]*\\}"));
    t.remove(tags);
    t = t.simplified();
    if (t.size() > 24)
        t = t.left(24) + QStringLiteral("…");
    return t;
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
    auto* help = new QPushButton(tr("❕"));
    help->setToolTip(tr("全部操作方式"));
    connect(zoomOut, &QPushButton::clicked, this,
            [this] { zoomAt(QPoint(width() / 2, 0), 1.0 / 1.25); });
    connect(zoomIn, &QPushButton::clicked, this,
            [this] { zoomAt(QPoint(width() / 2, 0), 1.25); });
    connect(fit, &QPushButton::clicked, this, [this] { fitToContent(); update(); });
    connect(help, &QPushButton::clicked, this, &TimelineView::showHelp);
    lay->addWidget(magnet);
    lay->addStretch(1);
    lay->addWidget(zoomOut);
    lay->addWidget(zoomIn);
    lay->addWidget(fit);
    lay->addWidget(help);
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
    discSuggests_ = s_->mplsSuggestedOffsets();
    discEds_ = s_->edAnchorTicks();
    const Cs gs = s_->globalShift();

    const QList<SegmentPoint> allPts = s_->segmentPoints();
    for (int i = 0; i < s_->count(); ++i) {
        const Session::Entry& e = s_->entry(i);
        RowInfo r;
        r.row = i;
        r.fileId = e.sub->id;
        r.start = e.offset + gs;
        // 内容范围（fileTime 域）：最早的非 Comment 非零时长行 = 首句；
        // 结束最晚的那行 = 末句（决定 47：标记 + 悬停台词）
        Cs first = -1;
        Cs lastEnd = -1;
        for (const EventLine& ev : e.doc->events) {
            if (ev.isComment || ev.zeroDuration())
                continue;
            if (first < 0 || ev.start < first) {
                first = ev.start;
                r.firstText = cleanLine(ev.text);
            }
            if (ev.end > lastEnd) {
                lastEnd = ev.end;
                r.lastStart = ev.start;
                r.lastText = cleanLine(ev.text);
            }
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

    // 防御：行数变化后选中越界即失效
    if (drag_ == Drag::None) {
        selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
                                        [this](int r) { return r >= rows_.size(); }),
                         selection_.end());
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
    // 盘标记叠加（决定 44）：默认不画——章节一直是吸附目标但不必常显
    // （"蓝点那么多"的反馈）；解析页"显示映射"开关开启才叠加：
    // 建议分集点 = 强调色线 + EP 标签；ED 锚定 = 黄色小三角；章节 = 极淡线
    if (discOverlay_) {
        for (Cs t : chapterTicks_) {
            const int x = timeToX(t);
            if (x < -10 || x > width() + 10)
                continue;
            p.setPen(QPen(QColor(0x4f, 0x8c, 0xff, 25), 1));
            p.drawLine(x, rulerBottom(), x, readoutTop());
        }
        for (Cs t : discEds_) {
            if (t < 0)
                continue;
            const int x = timeToX(t);
            if (x < -10 || x > width() + 10)
                continue;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xe8, 0xc2, 0x68));
            QPolygon tri;
            tri << QPoint(x - 4, rulerBottom() - 9) << QPoint(x + 4, rulerBottom() - 9)
                << QPoint(x, rulerBottom() - 2);
            p.drawPolygon(tri);
        }
        p.setFont(QFont(p.font().family(), 8));
        for (int k = 0; k < discSuggests_.size(); ++k) {
            const int x = timeToX(discSuggests_[k]);
            if (x < -30 || x > width() + 30)
                continue;
            p.setPen(QPen(QColor(0x4f, 0x8c, 0xff, 110), 1));
            p.drawLine(x, rulerBottom(), x, readoutTop());
            p.setPen(QColor(0x9d, 0xbe, 0xff));
            p.drawText(QRect(x - 24, rulerBottom() + 2, 48, 12), Qt::AlignHCenter,
                       QStringLiteral("EP%1").arg(k + 1));
        }
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
        // 首末句标记（决定 47）：首句开始（白▲，对轴主锚点——播放器听到的
        // 台词时刻对这里）、末句结束（黄▼，该集内容实际终点，检查是否压到
        // 下一集）。放大后可与标尺刻度精确对齐
        if (x2 - x1 > 26) {
            const int fx = timeToX(rowFinalStart(r) + ri.contentFirst + warpFirst);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xff, 0xff, 0xff, 230));
            QPolygon up;
            up << QPoint(fx - 4, trackTop() + 5) << QPoint(fx + 4, trackTop() + 5)
               << QPoint(fx, trackTop() + 12);
            p.drawPolygon(up);
            p.setBrush(QColor(0xe8, 0xc2, 0x68, 230));
            QPolygon down;
            down << QPoint(x2 - 5, trackTop() + kTrackH - 5)
                 << QPoint(x2 + 3, trackTop() + kTrackH - 5)
                 << QPoint(x2 - 1, trackTop() + kTrackH - 12);
            p.drawPolygon(down);
        }
        if (x2 - x1 > 34) {
            p.setPen(QColor(0xff, 0xff, 0xff, 220));
            p.drawText(QRect(x1 + 6, trackTop(), x2 - x1 - 8, kTrackH),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       tr("第%1集").arg(r + 1));
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
        left = tr("单击选块，拖动移动（后面的集跟着平移）；滚轮缩放");
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

void TimelineView::showHelp()
{
    // ❕ 速查表（决定 48）：三组操作一览，含"不可撤销"警示
    QDialog dlg(this);
    dlg.setWindowTitle(tr("操作方式"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* text = new QLabel(
        tr("<b>时间轴</b><br>"
           "拖动集块 = 移动该集（后面的集跟着平移）<br>"
           "块下方或标尺按住左键拖动 / 中键拖动 = 平移视口<br>"
           "滚轮 = 缩放（以光标为中心）｜ Ctrl+滚轮 = 平移<br>"
           "←→ = 移动选中块（±0.1 秒，Shift ±1 秒）；无选中时 = 平移<br>"
           "选中块后直接敲数字 = 移动到指定时刻（如 0:23:46）<br>"
           "Ctrl+Z = 撤销 ｜ F = 适配窗口<br>"
           "Delete = 移除选中的整集（<b>不可撤销</b>）<br><br>"
           "<b>文件</b><br>"
           "拖入文件/文件夹 = 添加 ｜ 列表中按住行拖动 = 排序<br>"
           "双击\"本集开始于\" = 直接输入数值（支持 0:24:09.45 / 24:09 / 秒数）<br><br>"
           "<b>原盘解析页</b><br>"
           "读取原盘 = 查看盘上的时间点（悬停看依据）<br>"
           "套用参考起点 = 仅供参考的一键填充，套用后可任意改"));
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(text);
    lay->addWidget(buttons);
    dlg.exec();
}

void TimelineView::mousePressEvent(QMouseEvent* e)
{
    // 中键任意处 = 平移（决定 41 三通道之一）
    if (e->button() == Qt::MiddleButton) {
        pressPos_ = e->pos();
        drag_ = Drag::Pan;
        panGrabTime_ = xToTime(e->pos().x());
        panGrabViewLeft_ = viewLeft_;
        dragMoved_ = false;
        return;
    }
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    pressPos_ = e->pos();
    dragMoved_ = false;
    previewStart_ = -1;
    snapTarget_ = -1;

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

    // 块下方区域 / 标尺带 = 左键平移（决定 41：主平移通道；
    // 该区域没有块与点，点命中已在前面处理）
    if ((e->pos().y() >= trackBottom() && e->pos().y() < readoutTop())
        || (e->pos().y() >= rulerTop() && e->pos().y() < rulerBottom())) {
        drag_ = Drag::Pan;
        panGrabTime_ = xToTime(e->pos().x());
        panGrabViewLeft_ = viewLeft_;
        return;
    }

    // 轨道带内空白：框选（Ctrl 不清除已有选择）
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
        const bool overPanZone =
            (e->pos().y() >= trackBottom() && e->pos().y() < readoutTop())
            || (e->pos().y() >= rulerTop() && e->pos().y() < rulerBottom());
        setCursor(overPanZone ? Qt::SizeHorCursor
                              : (hitBlock(e->pos()) >= 0 ? Qt::SizeAllCursor
                                                         : Qt::ArrowCursor));
        // 悬停块 → 首/末句台词（决定 47；通用悬停管道，内容以后随便扩展）。
        // 时刻用最终坐标——那就是"现在导出的话播放器会显示的时间"，
        // 与播放器读数直接可比；调轴闭环：听到台词时刻 ↔ 悬停看现在的位置
        const int hr = hitBlock(e->pos());
        if (hr != hoverRow_) {
            hoverRow_ = hr;
            if (hr < 0) {
                setToolTip(QString());
            } else {
                const RowInfo& ri = rows_[hr];
                const Cs base = rowFinalStart(hr);
                setToolTip(tr("第%1集\n"
                              "首句 %2 起「%3」\n"
                              "末句 %4 止「%5」")
                               .arg(hr + 1)
                               .arg(formatCs(base + ri.contentFirst),
                                    ri.firstText,
                                    formatCs(base + ri.maxEnd + ri.totalWarp),
                                    ri.lastText));
            }
        }
        update();   // 读数条光标时刻
        return;
    }

    if (drag_ == Drag::Pan) {
        // 抓着数轴走：按住点时刻跟随光标（决定 41）
        userAdjustedView_ = true;
        viewLeft_ = panGrabViewLeft_ + (panGrabTime_ - xToTime(e->pos().x()));
        update();
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
    // 左键与中键都结束拖动（中键平移的释放也必须复位，否则卡在平移态）
    if (e->button() != Qt::LeftButton && e->button() != Qt::MiddleButton)
        return;
    if (drag_ == Drag::Block && dragMoved_)
        commitBlockDrag(e->modifiers() & Qt::AltModifier);
    drag_ = Drag::None;
    dragRows_.clear();
    previewStart_ = -1;
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
        const bool left = e->key() == Qt::Key_Left;
        // 决定 41 两用语义：无选中（块与点都无）= 平移视口（长按连续），
        // 有选中 = ±0.1 秒移动选中块（Shift ±1 秒）
        if (selection_.isEmpty()) {
            userAdjustedView_ = true;
            const Cs step = Cs(40.0 / pxPerCs_)   // 一次约 40px，自动重复即连续滚动
                            * ((e->modifiers() & Qt::ShiftModifier) ? 4 : 1);
            viewLeft_ += left ? -step : step;
            update();
            return;
        }
        const Cs step = (e->modifiers() & Qt::ShiftModifier) ? Cs(100) : Cs(10);
        nudgeSelection(left ? -step : step,
                       e->modifiers() & Qt::AltModifier);
        return;
    }
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        removeSelectedEpisodes();
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

void TimelineView::nudgeSelection(Cs delta, bool solo)
{
    if (selection_.isEmpty())
        return;
    const Cs gs = s_->globalShift();
    QList<int> rows;
    QList<Cs> offs;
    // 与拖动同语义（决定 45）：单选 = 涟漪（该集及其后），Alt = 单块，
    // 多选 = 组移动
    if (selection_.size() == 1 && !solo) {
        const int r = selection_.first();
        for (int k = r; k < rows_.size(); ++k) {
            rows.append(k);
            offs.append(std::max(Cs(0), rows_[k].start + delta - gs));
        }
    } else {
        for (int r : selection_) {
            if (r < 0 || r >= rows_.size())
                continue;
            rows.append(r);
            offs.append(std::max(Cs(0), rows_[r].start + delta - gs));
        }
    }
    if (!rows.isEmpty())
        s_->setOffsets(rows, offs, true);
}

void TimelineView::removeSelectedEpisodes()
{
    if (selection_.isEmpty())
        return;
    // 决定 45：Delete = 移除选中的整集（结构变化：清撤销栈，不可 Ctrl+Z，
    // 帮助速查表中已标注）
    s_->removeRows(selection_);
    selection_.clear();
}

void TimelineView::commitBlockDrag(bool solo)
{
    if (dragRows_.isEmpty() || previewStart_ < 0)
        return;
    const Cs delta = previewStart_ - dragOrigStart_;
    const Cs gs = s_->globalShift();
    QList<int> rows;
    QList<Cs> offs;
    // 决定 45 涟漪语义：单选拖动 = 该集及其后全部平移（修好一个交界，
    // 后面的集顺势就位）；Alt+拖 = 只动该块；多选拖 = 组移动不涟漪
    const bool ripple = dragRows_.size() == 1 && !solo;
    const int from = ripple ? dragRows_.first() : 0;
    const int to = ripple ? rows_.size() : 0;
    for (int r : (ripple ? QList<int>() : dragRows_)) {
        if (r < 0 || r >= rows_.size())
            continue;
        rows.append(r);
        offs.append(std::max(Cs(0), rows_[r].start + delta - gs));
    }
    for (int r = from; r < to; ++r) {
        rows.append(r);
        offs.append(std::max(Cs(0), rows_[r].start + delta - gs));
    }
    if (!rows.isEmpty())
        s_->setOffsets(rows, offs, true);   // 一次手势 = 一步撤销
}

void TimelineView::cancelDrag()
{
    if (moveEdit_ && moveEdit_->isVisible()) {
        moveEdit_->hide();
        return;
    }
    drag_ = Drag::None;
    previewStart_ = -1;
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
            s_->setOffsetRipple(moveRow_, std::max(Cs(0), *cs - gs), true);
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
    if (const int r = hitBlock(e->pos()); r >= 0) {
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
