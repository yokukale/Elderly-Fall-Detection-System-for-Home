#ifndef VISION_CANVAS_WIDGET_H
#define VISION_CANVAS_WIDGET_H

#include "fall_types.h"
#include <QPointF>
#include <QVector>
#include <QWidget>

namespace qtfall {

class VisionCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VisionCanvasWidget(QWidget *parent = nullptr);

    void updateSnapshot(const FallStatusSnapshot &snapshot, bool hasFrame);
    void clearCanvas();

    QSize sizeHint() const override { return QSize(480, 360); }
    QSize minimumSizeHint() const override { return QSize(320, 240); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    FallStatusSnapshot m_snapshot;
    bool m_hasFrame = false;
    QVector<QPointF> m_trail;
};

} // namespace qtfall

#endif // VISION_CANVAS_WIDGET_H
