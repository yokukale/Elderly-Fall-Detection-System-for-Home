#ifndef WIN_SERIAL_PORT_H
#define WIN_SERIAL_PORT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

namespace qtfall {

struct SerialPortInfo {
    QString portName;
    QString friendlyName;

    QString displayLabel() const
    {
        if (friendlyName.isEmpty() || friendlyName == portName) {
            return portName;
        }
        return QStringLiteral("%1 (%2)").arg(portName, friendlyName);
    }
};

class WinSerialPort : public QObject
{
    Q_OBJECT
public:
    explicit WinSerialPort(QObject *parent = nullptr);
    ~WinSerialPort() override;

    static QVector<SerialPortInfo> availablePorts();

    bool openPort(const QString &portName,
                  quint32 baudRate = 115200,
                  int dataBits = 8,
                  int parity = 0,     // 0=None, 1=Odd, 2=Even
                  int stopBits = 1);  // 1=1bit, 2=2bits
    void closePort();
    bool isOpen() const;
    QString portName() const { return m_portName; }

    qint64 writeBytes(const QByteArray &data);
    quint64 txBytes() const { return m_txBytes; }
    quint64 rxBytes() const { return m_rxBytes; }

signals:
    void bytesReceived(const QByteArray &data);
    void portOpened(const QString &portName, quint32 baudRate);
    void portClosed();
    void errorOccurred(const QString &message);

private slots:
    void pollRead();

private:
    void *m_handle = nullptr; // HANDLE
    QTimer *m_pollTimer = nullptr;
    QString m_portName;
    quint64 m_txBytes = 0;
    quint64 m_rxBytes = 0;
};

} // namespace qtfall

#endif // WIN_SERIAL_PORT_H
