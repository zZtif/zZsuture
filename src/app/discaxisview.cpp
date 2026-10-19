#include "discaxisview.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include "time.hpp"

namespace suture {
namespace {

constexpr int kAxisY = 42;       // 主轴线 y
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
    suggests_ = s_->mplsSuggestedOffsets();
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
            return k;
    return -1;
}

void DiscAxisView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1f, 0x21, 0x27));

    if (total_ <= 0 || suggests_.isEmpty()) {
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
    p.setPen(QPen(QColor(0x8b, 0x8e, 0x9a), 2));
    p.drawLine(margin_, kAxisY - 4, margin_, kAxisY + 4);
    p.drawLine(width() - margin_, kAxisY - 4, width() - margin_, kAxisY + 4);
    p.setPen(QColor(0x8b, 0x8e, 0x9a));
    p.drawText(QRect(margin_ - 4, kAxisY + 8, 100, 14), Qt::AlignLeft,
               QStringLiteral("0:00"));
    p.drawText(QRect(width() - margin_ - 110, kAxisY + 8, 110, 14), Qt::AlignRight,
               formatCs(total_));

    // 建议分界点：大菱形 + EP 标签（链式分界点，整改文档一.2）
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

    // 图例 + 语义标注（推荐开始时间 = 块左端，不是第一句话）
    int lx = margin_;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4f, 0x8c, 0xff));
    QPolygon dia;
    dia << QPoint(lx + 4, kLegendY + 11) << QPoint(lx + 8, kLegendY + 7)
        << QPoint(lx + 4, kLegendY + 3) << QPoint(lx, kLegendY + 7);
    p.drawPolygon(dia);
    lx += 14;
    p.setPen(QColor(0x8b, 0x8e, 0x9a));
    p.drawText(QRect(lx, kLegendY, width() - lx - 8, 14), Qt::AlignLeft,
               tr("推荐开始时间 = 整个字幕块的开始位置（块左端），"
                  "不是第一句话的开始时间"));
}

void DiscAxisView::mouseMoveEvent(QMouseEvent* e)
{
    // 悬停详情（决定 43——依据摆出来，信不信由用户）
    const int idx = hitMarker(e->pos().x());
    if (idx < 0) {
        QToolTip::hideText();
        QWidget::mouseMoveEvent(e);
        return;
    }
    QString tip = tr("建议第 %1 集开始位置 %2（整个字幕块的左端，不是第一句"
                     "话）")
                      .arg(idx + 1)
                      .arg(formatCs(suggests_[idx]));
    if (idx < basis_.size() && !basis_[idx].isEmpty())
        tip += tr("\n依据：%1").arg(basis_[idx]);
    QToolTip::showText(e->globalPosition().toPoint(), tip, this);
}

}  // namespace suture
