#ifndef WINDOW_SLOTS_WIDGET_H
#define WINDOW_SLOTS_WIDGET_H

#include <QVector>
#include <QWidget>

namespace qtfall {

class WindowSlotsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WindowSlotsWidget(QWidget *parent = nullptr);

    void setWindowState(const QVector<bool> &slotsData, int winLen, int hitThreshold);

    QSize sizeHint() const override { return QSize(320, 32); }
    QSize minimumSizeHint() const override { return QSize(180, 26); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<bool> m_slots;
    int m_winLen = 10;
    int m_hitThreshold = 6;
};

} // namespace qtfall

#endif // WINDOW_SLOTS_WIDGET_H
