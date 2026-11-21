#include "fastbootclient.h"

// [ANDROID-FLASHER] 新增文件实现：Fastboot 文本协议（基于 WinUSB bulk 传输）

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace AndroidFlasher {

FastbootClient::FastbootClient() = default;
FastbootClient::~FastbootClient() = default;

void FastbootClient::setLogger(const LogFn &logger)
{
    m_logger = logger;
}

bool FastbootClient::open(const QString &devicePath, QString *error)
{
    return m_transport.open(devicePath, error);
}

void FastbootClient::close()
{
    m_transport.close();
}

bool FastbootClient::isOpen() const
{
    return m_transport.isOpen();
}

bool FastbootClient::sendCommand(const QString &command, QString *error)
{
    const QByteArray data = command.toLatin1();

    if(!m_transport.write(data, 8000, error)) {
        return false;
    }

    return true;
}

FastbootClient::Status FastbootClient::readStatus(QString *payload, QString *error)
{
    for(int attempt = 0; attempt < 32; ++attempt) {
        QString readError;
        const QByteArray packet = m_transport.read(256, 8000, &readError);

        if(packet.isEmpty()) {
            if(error) *error = readError.isEmpty() ? QStringLiteral("设备无响应") : readError;
            return Status::Invalid;
        }

        const QString text = QString::fromLatin1(packet);
        const QString status = text.left(4);

        if(status == QStringLiteral("INFO")) {
            if(m_logger) {
                m_logger(QStringLiteral("设备: %1").arg(text.mid(4).trimmed()));
            }
            continue;
        }

        if(status == QStringLiteral("OKAY")) {
            if(payload) *payload = text.mid(4);
            return Status::Okay;
        }

        if(status == QStringLiteral("FAIL")) {
            const QString reason = text.mid(4).trimmed();
            if(error) *error = reason.isEmpty() ? QStringLiteral("设备返回 FAIL") : reason;
            return Status::Fail;
        }

        if(status == QStringLiteral("DATA")) {
            if(payload) *payload = text.mid(4);
            return Status::Data;
        }

        if(error) {
            *error = QStringLiteral("无法识别的 Fastboot 响应：%1")
                    .arg(text.left(48).trimmed());
        }

        return Status::Invalid;
    }

    if(error) *error = QStringLiteral("Fastboot 响应次数过多，可能设备状态异常");

    return Status::Invalid;
}

bool FastbootClient::getvar(const QString &name, QString *value, QString *error)
{
    if(!sendCommand(QStringLiteral("getvar:") + name, error)) {
        return false;
    }

    QString payload;
    const Status status = readStatus(&payload, error);

    if(status != Status::Okay) {
        if(status == Status::Fail && error && error->isEmpty()) {
            *error = QStringLiteral("设备不支持 getvar:%1").arg(name);
        }
        return false;
    }

    if(value) {
        *value = payload.trimmed();
    }

    return true;
}

bool FastbootClient::flash(const QString &partition, const QString &filePath,
                           const ProgressFn &progress, QString *error)
{
    QFile file(filePath);

    if(!file.open(QIODevice::ReadOnly)) {
        if(error) *error = QStringLiteral("无法读取镜像文件：%1").arg(filePath);
        return false;
    }

    const qint64 total = file.size();

    if(total <= 0) {
        if(error) *error = QStringLiteral("镜像文件为空：%1").arg(QFileInfo(filePath).fileName());
        return false;
    }

    // 1) 申请下载：download:%08x
    if(!sendCommand(QStringLiteral("download:%1").arg(total, 8, 16, QLatin1Char('0')), error)) {
        return false;
    }

    QString payload;
    Status status = readStatus(&payload, error);

    if(status != Status::Data) {
        if(status == Status::Okay && error) {
            *error = QStringLiteral("设备没有进入数据接收状态");
        }
        return false;
    }

    const qint64 requested = payload.left(8).toLongLong(nullptr, 16);

    if(requested > 0 && requested != total) {
        if(error) {
            *error = QStringLiteral("设备要求的数据长度(%1)与镜像大小(%2)不一致，已中止")
                    .arg(requested).arg(total);
        }
        return false;
    }

    // 2) 上传数据（按 1 MiB 分块，便于进度刷新）
    qint64 written = 0;
    const qint64 chunkSize = 1024 * 1024;

    while(written < total) {
        const QByteArray chunk = file.read(static_cast<qint64>(qMin<qint64>(chunkSize, total - written)));

        if(chunk.isEmpty()) {
            if(error) *error = QStringLiteral("读取镜像过程中断");
            return false;
        }

        if(!m_transport.write(chunk, 8000, error)) {
            return false;
        }

        written += chunk.size();

        if(progress) {
            progress(static_cast<int>((written * 100) / total));
        }
    }

    // 3) 等待设备确认接收完成
    status = readStatus(&payload, error);

    if(status != Status::Okay) {
        if(status == Status::Fail) {
            return false;
        }

        if(error && error->isEmpty()) {
            *error = QStringLiteral("上传完成后设备未返回 OKAY");
        }
        return false;
    }

    // 4) 写入分区
    if(!sendCommand(QStringLiteral("flash:") + partition, error)) {
        return false;
    }

    status = readStatus(&payload, error);

    if(status == Status::Fail) {
        return false;
    }

    if(status != Status::Okay) {
        if(error) *error = QStringLiteral("flash:%1 未得到 OKAY 响应").arg(partition);
        return false;
    }

    return true;
}

bool FastbootClient::erase(const QString &partition, QString *error)
{
    if(!sendCommand(QStringLiteral("erase:") + partition, error)) {
        return false;
    }

    QString payload;
    const Status status = readStatus(&payload, error);

    if(status == Status::Fail) {
        return false;
    }

    if(status != Status::Okay) {
        if(error) *error = QStringLiteral("erase:%1 未得到 OKAY 响应").arg(partition);
        return false;
    }

    return true;
}

bool FastbootClient::reboot(const QString &target, QString *error)
{
    // [ANDROID-FLASHER] 新增 edl：一键重启到高通 9008（先试标准 reboot edl，再退回 oem edl）
    QStringList commands;

    if(target == QStringLiteral("bootloader")) {
        commands << QStringLiteral("reboot-bootloader");
    } else if(target == QStringLiteral("recovery")) {
        commands << QStringLiteral("reboot-recovery");
    } else if(target == QStringLiteral("fastbootd")) {
        commands << QStringLiteral("reboot-fastboot");
    } else if(target == QStringLiteral("edl")) {
        commands << QStringLiteral("reboot edl") << QStringLiteral("oem edl");
    } else {
        commands << QStringLiteral("reboot");
    }

    QString lastError;

    for(const QString &command : commands) {
        QString attemptError;

        if(!sendCommand(command, &attemptError)) {
            lastError = attemptError;
            continue;
        }

        QString payload;
        const Status status = readStatus(&payload, &attemptError);

        // 重启后设备会立刻断开，读不到 OKAY 属于正常现象，因此这里只要没有 FAIL 就算成功
        if(status != Status::Fail) {
            if(error) {
                error->clear();
            }

            return true;
        }

        lastError = attemptError;
    }

    if(error) {
        *error = lastError.isEmpty() ? QStringLiteral("重启失败：%1").arg(target) : lastError;
    }

    return false;
}

} // namespace AndroidFlasher
