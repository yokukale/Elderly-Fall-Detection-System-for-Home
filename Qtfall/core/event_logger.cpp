#include "event_logger.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace qtfall {

namespace {
constexpr int kMaxRecords = 5000;

QString csvEscape(const QString &field)
{
    QString out = field;
    out.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(out);
}
} // namespace

EventLogger::EventLogger(QObject *parent)
    : QObject(parent)
{
}

void EventLogger::addRecord(const AlarmEventRecord &record)
{
    AlarmEventRecord rec = record;
    if (rec.id <= 0) {
        rec.id = m_nextId++;
    } else if (rec.id >= m_nextId) {
        m_nextId = rec.id + 1;
    }
    if (!rec.timestamp.isValid()) {
        rec.timestamp = QDateTime::currentDateTime();
    }

    if (m_records.size() >= kMaxRecords) {
        m_records.removeFirst();
    }
    m_records.append(rec);
    emit recordAdded(rec);
}

void EventLogger::clear()
{
    m_records.clear();
    m_nextId = 1;
    emit cleared();
}

QByteArray EventLogger::formatCsvBytes(const QVector<AlarmEventRecord> &records)
{
    QByteArray out("\xEF\xBB\xBF"); // UTF-8 BOM
    out.append("ID,时间,来源,事件类型,置信度,检测框(x;y;w;h),备注\r\n");

    for (const auto &r : records) {
        const QString bboxText = QStringLiteral("[%1,%2,%3,%4]")
                                     .arg(r.bbox.x())
                                     .arg(r.bbox.y())
                                     .arg(r.bbox.width())
                                     .arg(r.bbox.height());
        const QString line = QStringLiteral("%1,%2,%3,%4,%5,%6,%7\r\n")
                                 .arg(r.id)
                                 .arg(csvEscape(r.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))))
                                 .arg(csvEscape(r.source))
                                 .arg(csvEscape(r.eventType))
                                 .arg(QString::number(r.confidence, 'f', 2))
                                 .arg(csvEscape(bboxText))
                                 .arg(csvEscape(r.note));
        out.append(line.toUtf8());
    }
    return out;
}

bool EventLogger::exportCsv(const QString &filePath) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray bytes = formatCsvBytes(m_records);
    return f.write(bytes) == bytes.size();
}

bool EventLogger::saveJson(const QString &filePath) const
{
    QJsonArray arr;
    for (const auto &r : m_records) {
        QJsonObject o;
        o[QStringLiteral("id")] = r.id;
        o[QStringLiteral("timestamp")] = r.timestamp.toString(Qt::ISODate);
        o[QStringLiteral("source")] = r.source;
        o[QStringLiteral("eventType")] = r.eventType;
        o[QStringLiteral("confidence")] = r.confidence;
        o[QStringLiteral("bbox")] = QJsonArray{r.bbox.x(), r.bbox.y(), r.bbox.width(), r.bbox.height()};
        o[QStringLiteral("note")] = r.note;
        arr.append(o);
    }

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray bytes = QJsonDocument(arr).toJson(QJsonDocument::Indented);
    return f.write(bytes) == bytes.size();
}

bool EventLogger::loadJson(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) {
        return false;
    }

    m_records.clear();
    m_nextId = 1;
    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        AlarmEventRecord r;
        r.id = o.value(QStringLiteral("id")).toInteger();
        r.timestamp = QDateTime::fromString(o.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
        r.source = o.value(QStringLiteral("source")).toString();
        r.eventType = o.value(QStringLiteral("eventType")).toString();
        r.confidence = o.value(QStringLiteral("confidence")).toDouble();
        const QJsonArray b = o.value(QStringLiteral("bbox")).toArray();
        if (b.size() == 4) {
            r.bbox = QRect(b[0].toInt(), b[1].toInt(), b[2].toInt(), b[3].toInt());
        }
        r.note = o.value(QStringLiteral("note")).toString();
        addRecord(r);
    }
    return true;
}

} // namespace qtfall
