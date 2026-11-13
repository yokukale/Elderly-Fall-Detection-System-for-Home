#include "vision_canvas_widget.h"

#include <QPainter>
#include <QPainterPath>

namespace qtfall {

VisionCanvasWidget::VisionCanvasWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VisionCanvasWidget::updateSnapshot(const FallStatusSnapshot &snapshot, bool hasFrame)
{
    m_snapshot = snapshot;
    m_hasFrame = hasFrame;

    if (hasFrame && snapshot.w > 0 && snapshot.h > 0) {
        const QPointF center(snapshot.x + snapshot.w * 0.5, snapshot.y + snapshot.h * 0.5);
        if (m_trail.isEmpty() || (m_trail.last() - center).manhattanLength() >= 2.0) {
            m_trail.append(center);
            if (m_trail.size() > 24) {
                m_trail.removeFirst();
            }
        }
    }
    update();
}

void VisionCanvasWidget::clearCanvas()
{
    m_snapshot = FallStatusSnapshot{};
    m_hasFrame = false;
    m_trail.clear();
    update();
}

void VisionCanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 整体卡片背景
    p.fillRect(rect(), QColor("#0B1220"));

    // 计算居中 4:3 (640x480) 画布区域
    const int margin = 12;
    const QRect avail = rect().adjusted(margin, margin, -margin, -margin);
    if (avail.width() <= 20 || avail.height() <= 20) {
        return;
    }

    double scale = qMin(static_cast<double>(avail.width()) / kK230FrameWidth,
                        static_cast<double>(avail.height()) / kK230FrameHeight);
    const int canvasW = qRound(kK230FrameWidth * scale);
    const int canvasH = qRound(kK230FrameHeight * scale);
    const QRect canvas(avail.x() + (avail.width() - canvasW) / 2,
                       avail.y() + (avail.height() - canvasH) / 2,
                       canvasW,
                       canvasH);

    // 4:3 视口背景与边框
    const QColor borderCol = m_snapshot.fallState ? QColor("#EF4444") : QColor("#334155");
    p.setPen(QPen(borderCol, m_snapshot.fallState ? 2.5 : 1.5));
    p.setBrush(QColor("#111C33"));
    p.drawRoundedRect(canvas, 8, 8);

    // 网格与参考线 (8x6)
    p.save();
    p.setClipRect(canvas);
    p.setPen(QPen(QColor(148, 163, 184, 30), 1));
    for (int i = 1; i < 8; ++i) {
        const int gx = canvas.x() + (canvas.width() * i) / 8;
        p.drawLine(gx, canvas.y(), gx, canvas.bottom());
    }
    for (int j = 1; j < 6; ++j) {
        const int gy = canvas.y() + (canvas.height() * j) / 6;
        p.drawLine(canvas.x(), gy, canvas.right(), gy);
    }

    // 地面参考线 (y = 320 / 480)
    const int floorY = canvas.y() + qRound(320.0 * scale);
    p.setPen(QPen(QColor(56, 189, 248, 60), 1, Qt::DashLine));
    p.drawLine(canvas.x(), floorY, canvas.right(), floorY);

    // 角落坐标标签
    p.setPen(QColor("#64748B"));
    QFont smallFont = font();
    smallFont.setPointSize(8);
    p.setFont(smallFont);
    p.drawText(canvas.adjusted(8, 6, -8, -6), Qt::AlignTop | Qt::AlignLeft, QStringLiteral("K230 视场 640×480"));
    p.drawText(canvas.adjusted(8, 6, -8, -6), Qt::AlignBottom | Qt::AlignRight, QStringLiteral("(640, 480)"));

    if (m_hasFrame && m_snapshot.w > 0 && m_snapshot.h > 0) {
        // 绘制质心运动轨迹
        if (m_trail.size() >= 2) {
            for (int i = 1; i < m_trail.size(); ++i) {
                const int alpha = 35 + (180 * i) / m_trail.size();
                p.setPen(QPen(QColor(56, 189, 248, alpha), 2));
                const QPointF p1(canvas.x() + m_trail[i - 1].x() * scale, canvas.y() + m_trail[i - 1].y() * scale);
                const QPointF p2(canvas.x() + m_trail[i].x() * scale, canvas.y() + m_trail[i].y() * scale);
                p.drawLine(p1, p2);
            }
        }

        // 选择检测框颜色
        QColor boxCol("#10B981");
        if (m_snapshot.fallState || m_snapshot.poseClass == PoseClass::Fall) {
            boxCol = QColor("#EF4444");
        } else if (m_snapshot.poseClass == PoseClass::SideLying || m_snapshot.sysState == FallSysState::Cooldown) {
            boxCol = QColor("#F59E0B");
        }

        const QRectF box(canvas.x() + m_snapshot.x * scale,
                         canvas.y() + m_snapshot.y * scale,
                         m_snapshot.w * scale,
                         m_snapshot.h * scale);

        QColor fillCol = boxCol;
        fillCol.setAlpha(m_snapshot.fallState ? 48 : 28);
        p.setBrush(fillCol);
        p.setPen(QPen(boxCol, 2.0));
        p.drawRoundedRect(box, 4, 4);

        // L 形角标
        const double corner = qMin(14.0, qMin(box.width(), box.height()) * 0.28);
        p.setPen(QPen(boxCol.lighter(130), 3.0));
        // 左上
        p.drawLine(QPointF(box.left(), box.top()), QPointF(box.left() + corner, box.top()));
        p.drawLine(QPointF(box.left(), box.top()), QPointF(box.left(), box.top() + corner));
        // 右上
        p.drawLine(QPointF(box.right(), box.top()), QPointF(box.right() - corner, box.top()));
        p.drawLine(QPointF(box.right(), box.top()), QPointF(box.right(), box.top() + corner));
        // 左下
        p.drawLine(QPointF(box.left(), box.bottom()), QPointF(box.left() + corner, box.bottom()));
        p.drawLine(QPointF(box.left(), box.bottom()), QPointF(box.left(), box.bottom() - corner));
        // 右下
        p.drawLine(QPointF(box.right(), box.bottom()), QPointF(box.right() - corner, box.bottom()));
        p.drawLine(QPointF(box.right(), box.bottom()), QPointF(box.right(), box.bottom() - corner));

        // 顶部状态标签条
        const bool geomOk = (m_snapshot.w > m_snapshot.h);
        const QString badge = QStringLiteral("%1  %2%  %3")
                                  .arg(poseClassLabel(m_snapshot.poseClass))
                                  .arg(qRound(m_snapshot.confidence * 100.0))
                                  .arg(geomOk ? QStringLiteral("w>h✓") : QStringLiteral("w≤h"));
        QFont badgeFont = font();
        badgeFont.setPointSize(9);
        badgeFont.setBold(true);
        p.setFont(badgeFont);
        const QFontMetrics fm(badgeFont);
        const int tagW = fm.horizontalAdvance(badge) + 14;
        const int tagH = fm.height() + 6;
        const double tagX = qBound<double>(canvas.left() + 4, box.left(), canvas.right() - tagW - 4);
        const double tagY = (box.top() - tagH - 3 >= canvas.top()) ? (box.top() - tagH - 3) : (box.top() + 4);
        const QRectF tagRect(tagX, tagY, tagW, tagH);
        p.setPen(Qt::NoPen);
        p.setBrush(boxCol);
        p.drawRoundedRect(tagRect, 4, 4);
        p.setPen(Qt::white);
        p.drawText(tagRect, Qt::AlignCenter, badge);
    }

    if (!m_hasFrame) {
        p.setPen(QColor("#94A3B8"));
        QFont tipFont = font();
        tipFont.setPointSize(10);
        p.setFont(tipFont);
        p.drawText(canvas, Qt::AlignCenter, QStringLiteral("等待 K230 视觉帧 / 串口数据 / 本地仿真..."));
    } else if (m_snapshot.visionOffline) {
        p.fillRect(canvas, QColor(15, 23, 42, 165));
        p.setPen(QColor("#FCA5A5"));
        QFont warnFont = font();
        warnFont.setPointSize(12);
        warnFont.setBold(true);
        p.setFont(warnFont);
        p.drawText(canvas, Qt::AlignCenter, QStringLiteral("⚠ K230 视觉信号超时掉线"));
    }

    p.restore();
}

} // namespace qtfall
