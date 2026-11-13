#include "win_serial_port.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <devguid.h>
#endif

#include <QMap>
#include <QRegularExpression>
#include <algorithm>

namespace qtfall {

WinSerialPort::WinSerialPort(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(10);
    connect(m_pollTimer, &QTimer::timeout, this, &WinSerialPort::pollRead);
}

WinSerialPort::~WinSerialPort()
{
    closePort();
}

QVector<SerialPortInfo> WinSerialPort::availablePorts()
{
    QVector<SerialPortInfo> list;
#ifdef Q_OS_WIN
    QMap<QString, QString> friendlyMap;

    // 1) 通过 SetupAPI 读取设备友好名称（如 USB-SERIAL CH340 (COM5)）
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devData;
        devData.cbSize = sizeof(SP_DEVINFO_DATA);
        for (DWORD idx = 0; SetupDiEnumDeviceInfo(devInfo, idx, &devData); ++idx) {
            wchar_t buf[256] = {0};
            if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_FRIENDLYNAME, nullptr,
                                                  reinterpret_cast<PBYTE>(buf), sizeof(buf), nullptr)) {
                const QString fname = QString::fromWCharArray(buf);
                static const QRegularExpression reCom(R"(\((COM\d+)\))", QRegularExpression::CaseInsensitiveOption);
                const auto m = reCom.match(fname);
                if (m.hasMatch()) {
                    friendlyMap.insert(m.captured(1).toUpper(), fname);
                }
            }
        }
        SetupDiDestroyDeviceInfoList(devInfo);
    }

    // 2) 通过注册表 DEVICEMAP\SERIALCOMM 读取真实在位串口
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t valName[256];
        wchar_t valData[256];
        for (DWORD idx = 0;; ++idx) {
            DWORD nameLen = 256;
            DWORD dataLen = sizeof(valData);
            DWORD type = 0;
            const LONG rc = RegEnumValueW(hKey, idx, valName, &nameLen, nullptr, &type,
                                          reinterpret_cast<LPBYTE>(valData), &dataLen);
            if (rc == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (rc == ERROR_SUCCESS && type == REG_SZ) {
                const QString port = QString::fromWCharArray(valData).trimmed().toUpper();
                if (!port.isEmpty()) {
                    SerialPortInfo info;
                    info.portName = port;
                    info.friendlyName = friendlyMap.value(port);
                    list.append(info);
                }
            }
        }
        RegCloseKey(hKey);
    }

    std::sort(list.begin(), list.end(), [](const SerialPortInfo &a, const SerialPortInfo &b) {
        return a.portName.mid(3).toInt() < b.portName.mid(3).toInt();
    });
#endif
    return list;
}

bool WinSerialPort::openPort(const QString &portName, quint32 baudRate, int dataBits, int parity, int stopBits)
{
    closePort();

#ifdef Q_OS_WIN
    const QString clean = portName.trimmed().toUpper();
    if (clean.isEmpty()) {
        emit errorOccurred(QStringLiteral("串口名称为空"));
        return false;
    }

    const QString winPath = QStringLiteral("\\\\.\\%1").arg(clean);
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(winPath.utf16()),
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           nullptr,
                           OPEN_EXISTING,
                           0,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        emit errorOccurred(QStringLiteral("打开 %1 失败 (Win32 错误码 %2)").arg(clean).arg(err));
        return false;
    }

    SetupComm(h, 8192, 4096);

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        emit errorOccurred(QStringLiteral("读取串口参数失败"));
        return false;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = static_cast<BYTE>(qBound(5, dataBits, 8));
    dcb.Parity = (parity == 1) ? ODDPARITY : ((parity == 2) ? EVENPARITY : NOPARITY);
    dcb.StopBits = (stopBits == 2) ? TWOSTOPBITS : ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = (parity != 0) ? TRUE : FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;

    if (!SetCommState(h, &dcb)) {
        const DWORD err = GetLastError();
        CloseHandle(h);
        emit errorOccurred(QStringLiteral("配置 %1 波特率/参数失败 (错误码 %2)").arg(clean).arg(err));
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 100;
    SetCommTimeouts(h, &timeouts);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    m_handle = h;
    m_portName = clean;
    m_txBytes = 0;
    m_rxBytes = 0;
    m_pollTimer->start();
    emit portOpened(clean, baudRate);
    return true;
#else
    Q_UNUSED(portName);
    Q_UNUSED(baudRate);
    Q_UNUSED(dataBits);
    Q_UNUSED(parity);
    Q_UNUSED(stopBits);
    return false;
#endif
}

void WinSerialPort::closePort()
{
    m_pollTimer->stop();
#ifdef Q_OS_WIN
    if (m_handle != nullptr) {
        HANDLE h = static_cast<HANDLE>(m_handle);
        m_handle = nullptr;
        CloseHandle(h);
        m_portName.clear();
        emit portClosed();
    }
#endif
}

bool WinSerialPort::isOpen() const
{
    return m_handle != nullptr;
}

qint64 WinSerialPort::writeBytes(const QByteArray &data)
{
#ifdef Q_OS_WIN
    if (m_handle == nullptr || data.isEmpty()) {
        return -1;
    }
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(m_handle),
                   data.constData(),
                   static_cast<DWORD>(data.size()),
                   &written,
                   nullptr)) {
        emit errorOccurred(QStringLiteral("串口写入失败 (错误码 %1)").arg(GetLastError()));
        return -1;
    }
    m_txBytes += written;
    return static_cast<qint64>(written);
#else
    Q_UNUSED(data);
    return -1;
#endif
}

void WinSerialPort::pollRead()
{
#ifdef Q_OS_WIN
    if (m_handle == nullptr) {
        return;
    }
    HANDLE h = static_cast<HANDLE>(m_handle);
    DWORD errors = 0;
    COMSTAT stat{};
    if (!ClearCommError(h, &errors, &stat)) {
        emit errorOccurred(QStringLiteral("串口连接异常断开"));
        closePort();
        return;
    }
    if (stat.cbInQue == 0) {
        return;
    }
    const DWORD toRead = qMin<DWORD>(stat.cbInQue, 4096);
    QByteArray buf(static_cast<int>(toRead), Qt::Uninitialized);
    DWORD actual = 0;
    if (ReadFile(h, buf.data(), toRead, &actual, nullptr) && actual > 0) {
        buf.resize(static_cast<int>(actual));
        m_rxBytes += actual;
        emit bytesReceived(buf);
    }
#endif
}

} // namespace qtfall
