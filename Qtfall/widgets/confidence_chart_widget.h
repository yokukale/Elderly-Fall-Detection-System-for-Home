#ifndef CONFIDENCE_CHART_WIDGET_H
#define CONFIDENCE_CHART_WIDGET_H

#include <QVector>
#include <QWidget>

namespace qtfall {

class ConfidenceChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ConfidenceChartWidget(QWidget *parent = nullptr);

    void appendSample(double confidence, bool isFallHit);
    void setThreshold(double th);
    void clearChart();

    QSize sizeHint() const override { return QSize(360, 150); }
    QSize minimumSizeHint() const override { return QSize(220, 110); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample {
        double value = 0.0;
        bool isFallHit = false;
    };

    QVector<Sample> m_samples;
    double m_threshold = 0.50;
};

} // namespace qtfall

#endif // CONFIDENCE_CHART_WIDGET_H
