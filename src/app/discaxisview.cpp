#include "discaxisview.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

#include "time.hpp"

namespace suture {
namespace {

constexpr int kAxisY = 42;       // 主轴线 y
constexpr int kTickH = 6;        // 普通章节刻度高
constexpr int kLegendY = 66;     // 图例行 y

}  // namespace

DiscAxisView::DiscAxisView(Session* session, QWidget* parent)
    : QWidget(parent), s_(session)
{
    setMouseTracking(true);
    setMinimumHeight(84);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    refresh();
}

void DiscAxisView::refresh()
{
    chapters_ = s_->chapterTicks();
    suggests_ = s_->mplsSuggestedOffsets();
    eds_ = s_->edAnchorTicks();
    basis_ = s_->mplsEpBasis();
    total_ = 0;
    if (const MplsInfo* m = s_->mpls())
        total_ = m->totalCs;
    update();
}

int DiscAxisView::timeToX(Cs t) const
{
    if (total_ <= 0)
        return margin_;
    return margin_ + int(double(t) / double(total_) * (width() - 2 * margin_));
}

Cs DiscAxisView::xToTime(int x) const
{
    if (width() <= 2 * margin_)
        return 0;
    return Cs(double(x - margin_) / double(width() - 2 * margin_) * double(total_));
}

int DiscAxisView::hitMarker(int x) const
{
    const auto near = [x](int mx) { return std::abs(mx - x) <= 6; };
    for (int k = 0; k < suggests_.size(); ++k)
        if (near(timeToX(suggests_[k])))
            return 1 * 100000 + k;
    for (int k = 0; k < eds_.size(); ++k)
        if (eds_[k] >= 0 && near(timeToX(eds_[k])))
            return 2 * 100000 + k;
    for (int k = 0; k < chapters_.size(); ++k)
        if (near(timeToX(chapters_[k])))
            return 0 * 100000 + k;
    return -1;
}

void DiscAxisView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1f, 0x21, 0x27));

    if (total_ <= 0 || (chapters_.isEmpty() && suggests_.isEmpty())) {
        p.setPen(QColor(0x5a, 0x5d, 0x68));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("读取原盘后，这里显示盘上的时间点"));
        return;
    }

    QFont f = p.font();
    f.setPointSizeF(9.0);
    p.setFont(f);

    // 主轴线（纯数轴：一条细线，无色块）
    p.setPen(QPen(QColor(0x8b, 0x8e, 0x9a), 1));
    p.drawLine(margin_, kAxisY, width() - margin_, kAxisY);
    // 首尾端点 + 总时长标注
    p.setPen(QPen(QColor(0x8b, 0x8e, 0x9a), 2));
    p.drawLine(margin_, kAxisY - 4, margin_, kAxisY + 4);
    p.drawLine(width() - margin_, kAxisY - 4, width() - margin_, kAxisY + 4);
    p.setPen(QColor(0x8b, 0x8e, 0x9a));
    p.drawText(QRect(margin_ - 4, kAxisY + 8, 100, 14), Qt::AlignLeft,
               QStringLiteral("0:00"));
    p.drawText(QRect(width() - margin_ - 110, kAxisY + 8, 110, 14), Qt::AlignRight,
               formatCs(total_));

    // 细刻度 = 全部章节（可读性第 1 层：形状分类）
    p.setPen(QPen(QColor(0x5a, 0x5d, 0x68), 1));
    for (Cs t : chapters_)
        p.drawLine(timeToX(t), kAxisY - kTickH, timeToX(t), kAxisY + kTickH);

    // ▽ 小三角（黄）= 疑似片尾曲起点（ED 锚定实际所用章节）
    for (Cs t : eds_) {
        if (t < 0)
            continue;
        const int x = timeToX(t);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xe8, 0xc2, 0x68));
        QPolygon tri;
        tri << QPoint(x - 4, kAxisY - 9) << QPoint(x + 4, kAxisY - 9)
            << QPoint(x, kAxisY - 2);
        p.drawPolygon(tri);
    }

    // ◆ 大菱形（强调色）= 建议分集点 + EP 标签（可读性第 2 层）
    for (int k = 0; k < suggests_.size(); ++k) {
        const int x = timeToX(suggests_[k]);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x4f, 0x8c, 0xff));
        QPolygon dia;
        dia << QPoint(x, kAxisY - 15) << QPoint(x + 6, kAxisY - 8)
            << QPoint(x, kAxisY - 1) << QPoint(x - 6, kAxisY - 8);
        p.drawPolygon(dia);
        p.setPen(QColor(0xdd, 0xe0, 0xe8));
        p.drawText(QRect(x - 30, kAxisY - 32, 60, 14), Qt::AlignHCenter,
                   QStringLiteral("EP%1").arg(k + 1));
    }

    // 图例（可读性第 3 层）
    p.setPen(QColor(0x8b, 0x8e, 0x9a));
    int lx = margin_;
    const auto legend = [&p, &lx](const QString& text) {
        p.drawText(QRect(lx, kLegendY, 400, 14), Qt::AlignLeft, text);
        lx += 16 + p.fontMetrics().horizontalAdvance(text);
    };
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4f, 0x8c, 0xff));
    QPolygon dia;
    dia << QPoint(lx + 4, kLegendY + 11) << QPoint(lx + 8, kLegendY + 7)
        << QPoint(lx + 4, kLegendY + 3) << QPoint(lx, kLegendY + 7);
    p.drawPolygon(dia);
    lx += 14;
    legend(tr("建议分集点"));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xe8, 0xc2, 0x68));
    QPolygon tri;
    tri << QPoint(lx, kLegendY + 3) << QPoint(lx + 8, kLegendY + 3)
        << QPoint(lx + 4, kLegendY + 9);
    p.drawPolygon(tri);
    lx += 14;
    legend(tr("疑似片尾曲起点"));
    p.setPen(QPen(QColor(0x5a, 0x5d, 0x68), 1));
    p.drawLine(lx + 4, kLegendY + 2, lx + 4, kLegendY + 11);
    lx += 12;
    legend(tr("章节"));
}

void DiscAxisView::mouseMoveEvent(QMouseEvent* e)
{
    // 可读性第 4 层：悬停详情（决定 43——依据摆出来，信不信由用户）
    const int h = hitMarker(e->pos().x());
    if (h < 0) {
        QToolTip::hideText();
        QWidget::mouseMoveEvent(e);
        return;
    }
    const int kind = h / 100000;
    const int idx = h % 100000;
    QString tip;
    if (kind == 1) {
        tip = tr("建议第 %1 集起点 %2").arg(idx + 1).arg(formatCs(suggests_[idx]));
        if (idx < basis_.size() && !basis_[idx].isEmpty())
            tip += tr("\n依据：%1").arg(basis_[idx]);
        else
            tip += tr("\n依据：章节位置估计（均匀估计）");
    } else if (kind == 2) {
        tip = tr("疑似片尾曲起点 %1\n（其后 90 秒处有结束章节——歌曲时长特征）")
                  .arg(formatCs(eds_[idx]));
    } else {
        tip = tr("章节 %1").arg(formatCs(chapters_[idx]));
    }
    QToolTip::showText(e->globalPosition().toPoint(), tip, this);
}

}  // namespace suture
