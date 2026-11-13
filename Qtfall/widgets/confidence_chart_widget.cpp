#include "confidence_chart_widget.h"

#include <QPainter>
#include <QPainterPath>

namespace qtfall {

ConfidenceChartWidget::ConfidenceChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ConfidenceChartWidget::appendSample(double confidence, bool isFallHit)
{
    m_samples.append(Sample{qBound(0.0, confidence, 1.0), isFallHit});
    if (m_samples.size() > 60) {
        m_samples.removeFirst();
    }
    update();
}

void ConfidenceChartWidget::setThreshold(double th)
{
    m_threshold = qBound(0.0, th, 1.0);
    update();
}

void ConfidenceChartWidget::clearChart()
{
    m_samples.clear();
    update();
}

void ConfidenceChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.fillRect(rect(), QColor("#0F172A"));

    const QRect plot = rect().adjusted(38, 18, -14, -18);
    if (plot.width() <= 20 || plot.height() <= 20) {
        return;
    }

    p.setPen(QPen(QColor("#334155"), 1));
    p.drawRect(plot);

    // Y 轴刻度
    QFont smallFont = font();
    smallFont.setPointSize(8);
    p.setFont(smallFont);
    p.setPen(QColor("#94A3B8"));
    p.drawText(QRect(2, plot.top() - 7, 32, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("1.0"));
    p.drawText(QRect(2, plot.center().y() - 7, 32, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0.5"));
    p.drawText(QRect(2, plot.bottom() - 7, 32, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0.0"));

    // 阈值虚线
    const int thY = plot.bottom() - qRound(m_threshold * plot.height());
    p.setPen(QPen(QColor("#F59E0B"), 1.2, Qt::DashLine));
    p.drawLine(plot.left(), thY, plot.right(), thY);
    p.setPen(QColor("#FBBF24"));
    p.drawText(plot.adjusted(4, 0, -4, 0), Qt::AlignTop | Qt::AlignLeft,
               QStringLiteral("置信度趋势 (阈值 %1)").arg(QString::number(m_threshold, 'f', 2)));

    if (m_samples.isEmpty()) {
        p.setPen(QColor("#64748B"));
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("暂无采样数据"));
        return;
    }

    const int n = m_samples.size();
    const double dx = (n > 1) ? static_cast<double>(plot.width()) / 59.0 : 0.0;
    const double startX = plot.right() - (n - 1) * dx;

    QPainterPath linePath;
    QPainterPath fillPath;

    for (int i = 0; i < n; ++i) {
        const double px = startX + i * dx;
        const double py = plot.bottom() - m_samples[i].value * plot.height();
        if (i == 0) {
            linePath.moveTo(px, py);
            fillPath.moveTo(px, plot.bottom());
            fillPath.lineTo(px, py);
        } else {
            linePath.lineTo(px, py);
            fillPath.lineTo(px, py);
        }
    }
    fillPath.lineTo(plot.right(), plot.bottom());
    fillPath.closeSubpath();

    QLinearGradient grad(plot.topLeft(), plot.bottomLeft());
    grad.setColorAt(0.0, QColor(56, 189, 248, 85));
    grad.setColorAt(1.0, QColor(56, 189, 248, 5));
    p.fillPath(fillPath, grad);

    p.setPen(QPen(QColor("#38BDF8"), 2.0));
    p.drawPath(linePath);

    // 跌倒命中点高亮
    for (int i = 0; i < n; ++i) {
        if (!m_samples[i].isFallHit) continue;
        const double px = startX + i * dx;
        const double py = plot.bottom() - m_samples[i].value * plot.height();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#EF4444"));
        p.drawEllipse(QPointF(px, py), 3.5, 3.5);
    }

    // 右上角最新数值
    const double latest = m_samples.last().value;
    p.setPen(m_samples.last().isFallHit ? QColor("#FCA5A5") : QColor("#7DD3FC"));
    p.drawText(plot.adjusted(0, 0, -4, 0), Qt::AlignTop | Qt::AlignRight,
               QStringLiteral("最新: %1").arg(QString::number(latest, 'f', 2)));
}

} // namespace qtfall
