#include "window_slots_widget.h"

#include <QPainter>

namespace qtfall {

WindowSlotsWidget::WindowSlotsWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(28);
}

void WindowSlotsWidget::setWindowState(const QVector<bool> &slotsData, int winLen, int hitThreshold)
{
    m_slots = slotsData;
    m_winLen = qBound(1, winLen, 30);
    m_hitThreshold = qBound(1, hitThreshold, m_winLen);
    update();
}

void WindowSlotsWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int labelW = 78;
    const QRect barRect = rect().adjusted(2, 4, -labelW - 6, -4);
    if (barRect.width() <= 20) {
        return;
    }

    const int gap = 4;
    const double cellW = (barRect.width() - gap * (m_winLen - 1)) / static_cast<double>(m_winLen);
    int hits = 0;

    for (int i = 0; i < m_winLen; ++i) {
        const QRectF cell(barRect.x() + i * (cellW + gap), barRect.y(), cellW, barRect.height());
        QColor fill("#334155"); // 尚未填充
        if (i < m_slots.size()) {
            if (m_slots[i]) {
                fill = QColor("#EF4444"); // 命中跌倒帧
                hits++;
            } else {
                fill = QColor("#10B981"); // 正常帧
            }
        }
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(cell, 3, 3);
    }

    const QRect textRect(barRect.right() + 6, rect().y(), labelW, rect().height());
    p.setPen(hits >= m_hitThreshold ? QColor("#F87171") : QColor("#CBD5E1"));
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("%1 / %2 (≥%3)").arg(hits).arg(m_winLen).arg(m_hitThreshold));
}

} // namespace qtfall
