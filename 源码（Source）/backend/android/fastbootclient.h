#pragma once

// [ANDROID-FLASHER] 新增文件：Fastboot 协议客户端
// 走 WinUSB bulk 端点，直接实现 fastboot 文本协议：
//   getvar / download / flash / erase / reboot

#include <QString>
#include <functional>

#include "androidusbtransport.h"

namespace AndroidFlasher {

class FastbootClient
{
public:
    using ProgressFn = std::function<void(int)>;
    using LogFn = std::function<void(const QString &)>;

    FastbootClient();
    ~FastbootClient();

    void setLogger(const LogFn &logger);

    bool open(const QString &devicePath, QString *error);
    void close();
    bool isOpen() const;

    bool getvar(const QString &name, QString *value, QString *error);
    bool flash(const QString &partition, const QString &filePath,
               const ProgressFn &progress, QString *error);
    bool erase(const QString &partition, QString *error);
    bool reboot(const QString &target, QString *error);

private:
    enum class Status {
        Okay,
        Fail,
        Info,
        Data,
        Invalid
    };

    bool sendCommand(const QString &command, QString *error);
    Status readStatus(QString *payload, QString *error);

    AndroidUsbTransport m_transport;
    LogFn m_logger;
};

} // namespace AndroidFlasher
