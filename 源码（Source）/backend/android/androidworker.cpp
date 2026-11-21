#include "androidworker.h"

// [ANDROID-FLASHER] 新增文件实现：在独立线程中执行 Fastboot / EDL 操作

#include <QFileInfo>

#include "edlclient.h"
#include "fastbootclient.h"
#include "rawprogramparser.h"

namespace AndroidFlasher {

AndroidWorker::AndroidWorker(QObject *parent):
    QObject(parent)
{}

void AndroidWorker::setEnglish(bool english)
{
    m_english = english;
}

QString AndroidWorker::loc(const QString &chinese, const QString &english) const
{
    return m_english ? english : chinese;
}

void AndroidWorker::cancel()
{
    m_cancel.storeRelaxed(1);
}

void AndroidWorker::fastbootFlash(const QString &devicePath, const QString &partition, const QString &filePath)
{
    m_cancel.storeRelaxed(0);
    emit progressChanged(0);

    FastbootClient client;
    client.setLogger([this](const QString &line) { emit logMessage(line); });

    QString error;

    if(!client.open(devicePath, &error)) {
        emit operationFinished(false, loc(QStringLiteral("无法打开 Fastboot 设备：%1").arg(error), QStringLiteral("Cannot open Fastboot device: %1").arg(error)));
        return;
    }

    const bool success = client.flash(partition, filePath,
                                      [this](int percent) { emit progressChanged(percent); },
                                      &error);

    client.close();

    if(success) {
        emit operationFinished(true, loc(QStringLiteral("%1 已写入分区 %2")
                                         .arg(QFileInfo(filePath).fileName(), partition),
                                         QStringLiteral("%1 written to %2")
                                         .arg(QFileInfo(filePath).fileName(), partition)));
    } else {
        emit operationFinished(false, error);
    }
}

void AndroidWorker::fastbootErase(const QString &devicePath, const QString &partition)
{
    m_cancel.storeRelaxed(0);
    emit progressChanged(0);

    FastbootClient client;
    client.setLogger([this](const QString &line) { emit logMessage(line); });

    QString error;

    if(!client.open(devicePath, &error)) {
        emit operationFinished(false, loc(QStringLiteral("无法打开 Fastboot 设备：%1").arg(error), QStringLiteral("Cannot open Fastboot device: %1").arg(error)));
        return;
    }

    const bool success = client.erase(partition, &error);
    client.close();

    emit operationFinished(success,
                           success ? loc(QStringLiteral("已擦除分区 %1").arg(partition), QStringLiteral("Erased %1").arg(partition)) : error);
}

void AndroidWorker::fastbootReboot(const QString &devicePath, const QString &target)
{
    m_cancel.storeRelaxed(0);
    emit progressChanged(0);

    FastbootClient client;
    client.setLogger([this](const QString &line) { emit logMessage(line); });

    QString error;

    if(!client.open(devicePath, &error)) {
        emit operationFinished(false, loc(QStringLiteral("无法打开 Fastboot 设备：%1").arg(error), QStringLiteral("Cannot open Fastboot device: %1").arg(error)));
        return;
    }

    const bool success = client.reboot(target, &error);
    client.close();

    emit operationFinished(success,
                           success ? loc(QStringLiteral("重启命令已发送（%1）").arg(target), QStringLiteral("Reboot sent (%1)").arg(target)) : error);
}

void AndroidWorker::fastbootQuery(const QString &devicePath)
{
    m_cancel.storeRelaxed(0);

    FastbootClient client;
    QString error;

    if(!client.open(devicePath, &error)) {
        return; // 查询失败不打扰用户，界面上仍显示 USB 描述信息
    }

    QString product;
    QString serial;

    if(client.getvar(QStringLiteral("product"), &product, &error)) {
        emit deviceVarsReady(devicePath, product, QString());
    }

    if(client.getvar(QStringLiteral("serialno"), &serial, &error)) {
        emit deviceVarsReady(devicePath, QString(), serial);
    }

    client.close();
}

void AndroidWorker::edlFlash(const QString &portName, const QString &rawProgramPath,
                             const QString &programmerPath)
{
    m_cancel.storeRelaxed(0);
    emit progressChanged(0);

    QVector<RawProgramEntry> entries;
    QString error;

    if(!RawProgramParser::parse(rawProgramPath, &entries, &error)) {
        emit operationFinished(false, error);
        return;
    }

    const QString baseDir = QFileInfo(rawProgramPath).absolutePath();

    EdlClient client;

    const bool success = client.flash(portName,
                                      programmerPath,
                                      entries,
                                      baseDir,
                                      [this](int percent) { emit progressChanged(percent); },
                                      [this](const QString &line) { emit logMessage(line); },
                                      &error);

    if(success) {
        emit operationFinished(true, loc(QStringLiteral("烧录完成，共写入 %1 个分区").arg(entries.size()), QStringLiteral("Flash finished, %1 partition(s) written").arg(entries.size())));
    } else {
        emit operationFinished(false, error.isEmpty() ? loc(QStringLiteral("烧录失败"), QStringLiteral("Flash failed")) : error);
    }
}

} // namespace AndroidFlasher
