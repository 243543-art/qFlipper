#pragma once

// [ANDROID-FLASHER] 新增文件：WinUSB 原始批量传输封装
// 只负责“打开设备 / 读写 bulk 端点 / 关闭”，Fastboot 与 EDL 协议层都复用它。

#include <QString>

class QByteArray;

namespace AndroidFlasher {

class AndroidUsbTransport
{
public:
    AndroidUsbTransport();
    ~AndroidUsbTransport();

    bool open(const QString &devicePath, QString *error);
    void close();
    bool isOpen() const;

    // 向 bulk OUT 端点写入（自动分块，带超时）
    bool write(const QByteArray &data, int timeoutMs, QString *error);

    // 从 bulk IN 端点读取一包，最多 maxSize 字节
    QByteArray read(int maxSize, int timeoutMs, QString *error);

    // 传输异常后复位端点，避免后续请求全部失败
    void resetPipes();

private:
    struct Private;
    Private *d;
};

} // namespace AndroidFlasher
