#ifndef EVENT_LOGGER_H
#define EVENT_LOGGER_H

#include "fall_types.h"
#include <QObject>
#include <QVector>

namespace qtfall {

class EventLogger : public QObject
{
    Q_OBJECT
public:
    explicit EventLogger(QObject *parent = nullptr);

    void addRecord(const AlarmEventRecord &record);
    void clear();
    QVector<AlarmEventRecord> records() const { return m_records; }
    int count() const { return m_records.size(); }

    bool exportCsv(const QString &filePath) const;
    bool saveJson(const QString &filePath) const;
    bool loadJson(const QString &filePath);

    static QByteArray formatCsvBytes(const QVector<AlarmEventRecord> &records);

signals:
    void recordAdded(const qtfall::AlarmEventRecord &record);
    void cleared();

private:
    QVector<AlarmEventRecord> m_records;
    qint64 m_nextId = 1;
};

} // namespace qtfall

#endif // EVENT_LOGGER_H
