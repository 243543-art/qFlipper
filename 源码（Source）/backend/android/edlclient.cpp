#include "edlclient.h"

// [ANDROID-FLASHER] 新增文件实现：Sahara + Firehose 烧录

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QStringList>
#include <QWaitCondition>
#include <QtEndian>

#include "androiddevicescanner.h"

namespace AndroidFlasher {

namespace {

// Sahara 命令字（v2）
const quint32 SAHARA_HELLO_REQ      = 0x01;
const quint32 SAHARA_HELLO_RSP      = 0x02;
const quint32 SAHARA_READ_DATA      = 0x03;
const quint32 SAHARA_END_TRANSFER   = 0x04;
const quint32 SAHARA_DONE_REQ       = 0x05;
const quint32 SAHARA_DONE_RSP       = 0x06;
const quint32 SAHARA_64BIT_READ     = 0x12;

const int SAHARA_PACKET_HEADER_SIZE = 8;
const int SAHARA_WRITE_CHUNK        = 8192;

QString hexBytes(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' '));
}

// 阻塞延时（Qt5 中 QThread::msleep 为 protected，故用 QWaitCondition 实现）
void sleepMs(int milliseconds)
{
    QMutex mutex;
    QWaitCondition condition;

    mutex.lock();
    condition.wait(&mutex, static_cast<unsigned long>(milliseconds));
    mutex.unlock();
}

} // namespace

EdlClient::EdlClient()
{}

EdlClient::~EdlClient()
{
    closePort();
}

void EdlClient::cancel()
{
    m_cancel.storeRelaxed(1);
}

bool EdlClient::openPort(const QString &portName, QString *error)
{
    closePort();

    m_port.setPortName(portName);
    m_port.setBaudRate(115200);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);

    if(!m_port.open(QIODevice::ReadWrite)) {
        if(error) *error = QStringLiteral("无法打开串口 %1：%2").arg(portName, m_port.errorString());
        return false;
    }

    m_port.clear();
    m_rxBuffer.clear();
    m_portName = portName;
    m_firehoseMode = false;

    return true;
}

void EdlClient::closePort()
{
    if(m_port.isOpen()) {
        m_port.clear();
        m_port.close();
    }

    m_rxBuffer.clear();
}

bool EdlClient::readExact(QByteArray *out, int count, int timeoutMs, QString *error)
{
    QElapsedTimer timer;
    timer.start();

    while(m_rxBuffer.size() < count) {
        if(m_cancel.loadRelaxed()) {
            if(error) *error = QStringLiteral("操作已取消");
            return false;
        }

        if(m_port.waitForReadyRead(qMin(200, timeoutMs))) {
            m_rxBuffer.append(m_port.readAll());
            continue;
        }

        if(timer.elapsed() >= timeoutMs) {
            if(error) {
                *error = QStringLiteral("串口读取超时（需要 %1 字节，已有 %2 字节）")
                        .arg(count).arg(m_rxBuffer.size());
            }
            return false;
        }
    }

    *out = m_rxBuffer.left(count);
    m_rxBuffer.remove(0, count);

    return true;
}

bool EdlClient::writeRaw(const QByteArray &data, int timeoutMs, QString *error)
{
    int offset = 0;

    while(offset < data.size()) {
        if(m_cancel.loadRelaxed()) {
            if(error) *error = QStringLiteral("操作已取消");
            return false;
        }

        const int chunk = static_cast<int>(
                    qMin<qint64>(SAHARA_WRITE_CHUNK, static_cast<qint64>(data.size()) - offset));
        const QByteArray part = data.mid(offset, chunk);

        if(m_port.write(part) < 0) {
            if(error) *error = QStringLiteral("串口写入失败：%1").arg(m_port.errorString());
            return false;
        }

        if(!m_port.waitForBytesWritten(timeoutMs)) {
            if(error) *error = QStringLiteral("串口写入超时：%1").arg(m_port.errorString());
            return false;
        }

        offset += chunk;
    }

    return true;
}

bool EdlClient::readSaharaPacket(quint32 *command, QByteArray *payload, int timeoutMs, QString *error)
{
    QByteArray header;

    if(!readExact(&header, SAHARA_PACKET_HEADER_SIZE, timeoutMs, error)) {
        return false;
    }

    *command = readLe32(header, 0);
    const quint32 length = readLe32(header, 4);

    if(length < SAHARA_PACKET_HEADER_SIZE || length > 16 * 1024 * 1024) {
        if(error) {
            *error = QStringLiteral("无效的 Sahara 包长度：%1（命令 0x%2）")
                    .arg(length).arg(*command, 0, 16);
        }
        return false;
    }

    payload->resize(static_cast<int>(length) - SAHARA_PACKET_HEADER_SIZE);

    if(!payload->isEmpty() &&
       !readExact(payload, static_cast<int>(payload->size()), timeoutMs, error)) {
        return false;
    }

    return true;
}

bool EdlClient::handshakeSahara(const LogFn &log, QString *error)
{
    QByteArray header;

    if(!readExact(&header, SAHARA_PACKET_HEADER_SIZE, 3000, error)) {
        if(error) {
            *error = QStringLiteral("未收到 9008 握手数据，请确认设备处于 9008（EDL）模式且串口正确");
        }
        return false;
    }

    // 设备可能已经被 QFIL 载入了 programmer，直接进入 Firehose 模式
    if(header.startsWith("<")) {
        m_firehoseMode = true;

        if(log) {
            log(QStringLiteral("设备已处于 Firehose 模式，跳过 Sahara 握手"));
        }

        m_rxBuffer.prepend(header);

        return true;
    }

    const quint32 command = readLe32(header, 0);
    const quint32 length = readLe32(header, 4);

    if(command != SAHARA_HELLO_REQ) {
        if(error) {
            *error = QStringLiteral("不是预期的 Sahara HELLO，收到命令 0x%1（数据：%2）")
                    .arg(command, 0, 16).arg(hexBytes(header.left(16)));
        }
        return false;
    }

    if(length < SAHARA_PACKET_HEADER_SIZE || length > 4096) {
        if(error) *error = QStringLiteral("异常的 Sahara HELLO 长度：%1").arg(length);
        return false;
    }

    QByteArray payload(static_cast<int>(length) - SAHARA_PACKET_HEADER_SIZE, 0);

    if(!payload.isEmpty() && !readExact(&payload, static_cast<int>(payload.size()), 3000, error)) {
        return false;
    }

    const quint32 version = readLe32(payload, 0);
    const quint32 versionSupported = readLe32(payload, 4);
    const quint32 maxPacketSize = readLe32(payload, 8);
    const quint32 mode = readLe32(payload, 12);

    if(log) {
        log(QStringLiteral("Sahara HELLO：协议版本 %1，最低支持 %2，最大包长 %3，模式 %4")
            .arg(version).arg(versionSupported).arg(maxPacketSize).arg(mode));
    }

    m_firehoseMode = false;

    return true;
}

bool EdlClient::uploadLoader(const QString &loaderPath, const ProgressFn &progress,
                             const LogFn &log, QString *error)
{
    QFile loader(loaderPath);

    if(!loader.open(QIODevice::ReadOnly)) {
        if(error) *error = QStringLiteral("无法读取 programmer 文件：%1").arg(loaderPath);
        return false;
    }

    const qint64 loaderSize = loader.size();

    if(loaderSize <= 0) {
        if(error) *error = QStringLiteral("programmer 文件为空");
        return false;
    }

    // Sahara HELLO_RSP：版本 2 / 最低 1 / 进入命令模式
    QByteArray helloResponse(48, 0);
    writeLe32(helloResponse, 0, SAHARA_HELLO_RSP);
    writeLe32(helloResponse, 4, 0x30);
    writeLe32(helloResponse, 8, 2);
    writeLe32(helloResponse, 12, 1);
    writeLe32(helloResponse, 16, 0);
    writeLe32(helloResponse, 20, 0);

    for(int i = 0; i < 6; ++i) {
        writeLe32(helloResponse, 24 + i * 4, static_cast<quint32>(i + 1));
    }

    if(!writeRaw(helloResponse, 3000, error)) {
        return false;
    }

    qint64 reportedBytes = 0;
    bool loaderFinished = false;

    while(true) {
        quint32 command = 0;
        QByteArray payload;

        if(!readSaharaPacket(&command, &payload, 8000, error)) {
            return false;
        }

        switch(command) {
        case SAHARA_HELLO_REQ:
            // 多阶段握手：再回一次 HELLO_RSP
            if(!writeRaw(helloResponse, 3000, error)) {
                return false;
            }
            break;

        case SAHARA_READ_DATA:
        case SAHARA_64BIT_READ: {
            qint64 dataOffset = 0;
            qint64 dataLength = 0;

            if(command == SAHARA_READ_DATA) {
                if(payload.size() < 12) {
                    if(error) *error = QStringLiteral("Sahara READ_DATA 包字段不足");
                    return false;
                }

                dataOffset = readLe32(payload, 4);
                dataLength = readLe32(payload, 8);
            } else {
                if(payload.size() < 24) {
                    if(error) *error = QStringLiteral("Sahara 64bit READ 包字段不足");
                    return false;
                }

                dataOffset = static_cast<qint64>(readLe64(payload, 8));
                dataLength = static_cast<qint64>(readLe64(payload, 16));
            }

            if(dataOffset < 0 || dataLength <= 0 || dataLength > 512LL * 1024 * 1024) {
                if(error) {
                    *error = QStringLiteral("Sahara 请求的数据范围异常：offset=%1 length=%2")
                            .arg(dataOffset).arg(dataLength);
                }
                return false;
            }

            loader.seek(dataOffset);

            QByteArray chunk = loader.read(dataLength);

            if(chunk.size() < dataLength) {
                // 超出文件末尾的部分用 0xFF 填满（与 QFIL 行为一致）
                chunk.append(QByteArray(static_cast<int>(dataLength) - static_cast<int>(chunk.size()), static_cast<char>(0xFF)));
            }

            const int chunkSize = SAHARA_WRITE_CHUNK;
            int sent = 0;

            while(sent < chunk.size()) {
                if(m_cancel.loadRelaxed()) {
                    if(error) *error = QStringLiteral("操作已取消");
                    return false;
                }

                const QByteArray part = chunk.mid(sent, static_cast<int>(
                            qMin<qint64>(chunkSize, static_cast<qint64>(chunk.size()) - sent)));

                if(!writeRaw(part, 8000, error)) {
                    return false;
                }

                sent += part.size();
                reportedBytes += part.size();

                if(progress) {
                    progress(static_cast<int>(qMin<qint64>(9, (reportedBytes * 10) / loaderSize)));
                }
            }

            break;
        }

        case SAHARA_END_TRANSFER: {
            // 负载：image_id(4) + image_tx_status(4)
            const quint32 status = payload.size() >= 8 ? readLe32(payload, 4) : 1;

            if(status != 0) {
                if(error) *error = QStringLiteral("programmer 传输失败，Sahara 状态码 %1").arg(status);
                return false;
            }

            QByteArray doneRequest(SAHARA_PACKET_HEADER_SIZE, 0);
            writeLe32(doneRequest, 0, SAHARA_DONE_REQ);
            writeLe32(doneRequest, 4, SAHARA_PACKET_HEADER_SIZE);

            if(!writeRaw(doneRequest, 3000, error)) {
                return false;
            }

            quint32 doneCommand = 0;
            QByteArray donePayload;

            if(!readSaharaPacket(&doneCommand, &donePayload, 5000, error)) {
                return false;
            }

            if(doneCommand != SAHARA_DONE_RSP) {
                if(error) {
                    *error = QStringLiteral("Sahara 未返回 DONE_RSP（收到 0x%1）").arg(doneCommand, 0, 16);
                }
                return false;
            }

            loaderFinished = true;
            break;
        }

        default:
            if(log) {
                log(QStringLiteral("忽略 Sahara 命令 0x%1（%2 字节）")
                    .arg(command, 0, 16).arg(payload.size()));
            }
            break;
        }

        if(loaderFinished) {
            break;
        }
    }

    if(progress) {
        progress(10);
    }

    if(log) {
        log(QStringLiteral("programmer 上传完成（%1 KB）").arg(loaderSize / 1024));
    }

    return true;
}

bool EdlClient::waitForFirehosePort(const LogFn &log, QString *error)
{
    for(int attempt = 0; attempt < 40; ++attempt) {
        if(m_cancel.loadRelaxed()) {
            if(error) *error = QStringLiteral("操作已取消");
            return false;
        }

        sleepMs(500);

        QString candidate = m_portName;

        if(!AndroidDeviceScanner::availableSerialPorts().contains(candidate)) {
            candidate = AndroidDeviceScanner::findQualcommPort();
        }

        if(candidate.isEmpty()) {
            continue;
        }

        QString openError;

        if(openPort(candidate, &openError)) {
            if(log) {
                log(QStringLiteral("已重新连接 Firehose 串口 %1").arg(candidate));
            }
            return true;
        }

        if(log && attempt % 4 == 0) {
            log(QStringLiteral("等待 Firehose 端口就绪…（%1）").arg(openError));
        }
    }

    if(error) *error = QStringLiteral("programmer 启动后无法重新打开 9008 串口，请重新插拔设备后重试");

    return false;
}

QByteArray EdlClient::readFirehoseResponse(int timeoutMs)
{
    QByteArray collected;
    QElapsedTimer timer;
    timer.start();

    while(timer.elapsed() < timeoutMs) {
        if(m_cancel.loadRelaxed()) {
            break;
        }

        if(m_rxBuffer.isEmpty()) {
            if(!m_port.waitForReadyRead(qMin(200, timeoutMs))) {
                continue;
            }
        }

        if(!m_rxBuffer.isEmpty()) {
            collected.append(m_rxBuffer);
            m_rxBuffer.clear();
        }

        const int start = collected.indexOf("<response");

        if(start < 0) {
            continue;
        }

        const int endTag = collected.indexOf("</response>", start);

        if(endTag >= 0) {
            break;
        }

        const int selfClosing = collected.indexOf("/>", start);

        if(selfClosing >= 0) {
            const int nextResponse = collected.indexOf("<response", start + 1);

            if(nextResponse < 0 || nextResponse > selfClosing) {
                break;
            }
        }
    }

    return collected;
}

bool EdlClient::sendXml(const QString &xml, QByteArray *response, QString *error, int timeoutMs)
{
    if(!writeRaw(xml.toUtf8(), 6000, error)) {
        return false;
    }

    const QByteArray data = readFirehoseResponse(timeoutMs);

    if(data.isEmpty()) {
        if(error) *error = QStringLiteral("Firehose 无响应（命令：%1）").arg(xml.left(64));
        return false;
    }

    if(response) {
        *response = data;
    }

    if(!xmlIsAck(data)) {
        const QString logs = xmlLogs(data);

        if(error) {
            *error = logs.isEmpty() ?
                     QStringLiteral("Firehose 拒绝执行命令：%1").arg(xml.left(64)) :
                     QStringLiteral("Firehose 返回错误：%1").arg(logs);
        }
        return false;
    }

    return true;
}

bool EdlClient::configure(int *maxPayload, const LogFn &log, QString *error)
{
    const QStringList memories = {QStringLiteral("emmc"), QStringLiteral("ufs")};
    QString lastError;

    for(const QString &memory : memories) {
        if(m_cancel.loadRelaxed()) {
            if(error) *error = QStringLiteral("操作已取消");
            return false;
        }

        const QString xml = QStringLiteral(
            "<?xml version=\"1.0\" ?>\n"
            "<configure MemoryName=\"%1\" Verbose=\"0\" AlwaysValidate=\"0\" "
            "MaxDigestTableSizeInBytes=\"2048\" MaxPayloadSizeToTargetInBytes=\"1048576\" "
            "ZLPAwareHost=\"1\" SkipStorageInit=\"0\" SkipWrite=\"0\" />").arg(memory);

        QByteArray response;

        if(!sendXml(xml, &response, &lastError, 20000)) {
            if(log) {
                log(QStringLiteral("configure(%1) 失败：%2").arg(memory, lastError));
            }
            continue;
        }

        const QString reportedMaxPayload = xmlAttribute(response, QStringLiteral("MaxPayloadSizeToTargetInBytes"));

        if(!reportedMaxPayload.isEmpty() && maxPayload) {
            const int value = reportedMaxPayload.toInt();

            if(value >= 64 * 1024) {
                *maxPayload = qMin(value, 16 * 1024 * 1024);
            }
        }

        if(log) {
            log(QStringLiteral("Firehose 已连接：%1，单次负载 %2 字节")
                .arg(xmlAttribute(response, QStringLiteral("MemoryName")).isEmpty() ?
                     memory : xmlAttribute(response, QStringLiteral("MemoryName")))
                .arg(maxPayload ? *maxPayload : 0));
        }

        return true;
    }

    if(error) {
        *error = lastError.isEmpty() ? QStringLiteral("Firehose configure 失败") : lastError;
    }

    return false;
}

bool EdlClient::programEntry(const RawProgramEntry &entry, const QString &baseDir, int maxPayload,
                             const ProgressFn &progress, const LogFn &log, QString *error)
{
    const QString filePath = QDir(baseDir).filePath(entry.filename);
    QFile image(filePath);

    if(!image.open(QIODevice::ReadOnly)) {
        if(error) *error = QStringLiteral("缺少镜像文件：%1").arg(filePath);
        return false;
    }

    const qint64 fileSize = image.size();
    qint64 programSize = entry.sizeInBytes > 0 ? entry.sizeInBytes : fileSize;

    if(programSize <= 0) {
        if(error) *error = QStringLiteral("镜像文件为空：%1").arg(entry.filename);
        return false;
    }

    if(programSize > fileSize) {
        if(error) {
            *error = QStringLiteral("镜像文件 %1 实际大小(%2)小于 rawprogram 声明(%3)")
                    .arg(entry.filename).arg(fileSize).arg(programSize);
        }
        return false;
    }

    qint64 numSectors = entry.numSectors;

    if(numSectors <= 0) {
        numSectors = (programSize + entry.sectorSize - 1) / entry.sectorSize;
    }

    const QString xml = QStringLiteral(
        "<?xml version=\"1.0\" ?>\n"
        "<data>\n"
        "<program SECTOR_SIZE_IN_BYTES=\"%1\" num_partition_sectors=\"%2\" "
        "physical_partition_number=\"%3\" start_sector=\"%4\" />\n"
        "</data>").arg(entry.sectorSize)
                   .arg(numSectors)
                   .arg(entry.physicalPartition)
                   .arg(entry.startSector);

    if(!sendXml(xml, nullptr, error, 20000)) {
        return false;
    }

    if(log) {
        log(QStringLiteral("开始写入 %1（起始扇区 %2，%3 字节）")
            .arg(entry.label).arg(entry.startSector).arg(programSize));
    }

    qint64 sent = 0;

    while(sent < programSize) {
        if(m_cancel.loadRelaxed()) {
            if(error) *error = QStringLiteral("操作已取消");
            return false;
        }

        const qint64 remaining = programSize - sent;
        const qint64 chunkSize = qMin<qint64>(maxPayload, remaining);

        QByteArray data = image.read(chunkSize);

        if(data.isEmpty()) {
            if(error) *error = QStringLiteral("读取镜像数据失败：%1").arg(entry.filename);
            return false;
        }

        if(sent + data.size() >= programSize) {
            // 末尾补齐到扇区整数倍，避免设备按扇区写入时截断
            const int remainder = data.size() % entry.sectorSize;

            if(remainder != 0) {
                data.append(QByteArray(entry.sectorSize - remainder, 0));
            }
        }

        if(!writeRaw(data, 30000, error)) {
            return false;
        }

        sent += chunkSize;

        if(progress) {
            progress(static_cast<int>((sent * 100) / programSize));
        }
    }

    const QByteArray finalResponse = readFirehoseResponse(60000);

    if(finalResponse.isEmpty() || !xmlIsAck(finalResponse)) {
        if(error) {
            const QString logs = xmlLogs(finalResponse);
            *error = logs.isEmpty() ?
                     QStringLiteral("%1 写入后没有收到 Firehose ACK").arg(entry.label) :
                     QStringLiteral("%1 写入失败：%2").arg(entry.label, logs);
        }
        return false;
    }

    if(progress) {
        progress(100);
    }

    return true;
}

bool EdlClient::powerReset(const LogFn &log, QString *error)
{
    const QString xml = QStringLiteral("<?xml version=\"1.0\" ?>\n<power value=\"reset\" />");

    if(!sendXml(xml, nullptr, error, 10000)) {
        return false;
    }

    if(log) {
        log(QStringLiteral("已发送重启命令，设备将退出 9008 模式"));
    }

    return true;
}

bool EdlClient::flash(const QString &portName,
                      const QString &loaderPath,
                      const QVector<RawProgramEntry> &entries,
                      const QString &baseDir,
                      const ProgressFn &progress,
                      const LogFn &log,
                      QString *error)
{
    m_cancel.storeRelaxed(0);

    if(entries.isEmpty()) {
        if(error) *error = QStringLiteral("没有可烧录的分区条目");
        return false;
    }

    if(loaderPath.isEmpty() || !QFileInfo::exists(loaderPath)) {
        if(error) *error = QStringLiteral("未找到 firehose programmer 文件：%1").arg(loaderPath);
        return false;
    }

    if(!openPort(portName, error)) {
        return false;
    }

    if(progress) {
        progress(0);
    }

    if(!handshakeSahara(log, error)) {
        closePort();
        return false;
    }

    if(!m_firehoseMode) {
        if(log) {
            log(QStringLiteral("Sahara 握手成功，开始上传 programmer"));
        }

        if(!uploadLoader(loaderPath, progress, log, error)) {
            closePort();
            return false;
        }

        const QString previousPort = m_portName;
        closePort();

        if(!waitForFirehosePort(log, error)) {
            return false;
        }

        if(log && m_portName != previousPort) {
            log(QStringLiteral("串口从 %1 切换到 %2").arg(previousPort, m_portName));
        }
    }

    int maxPayload = 1024 * 1024;

    if(!configure(&maxPayload, log, error)) {
        closePort();
        return false;
    }

    // 预检查所有镜像文件，避免烧到一半才发现缺文件
    QVector<qint64> fileSizes;
    qint64 totalBytes = 0;

    for(const auto &entry : entries) {
        const QString filePath = QDir(baseDir).filePath(entry.filename);
        const qint64 size = QFileInfo(filePath).size();

        if(size <= 0) {
            if(error) *error = QStringLiteral("缺少镜像文件：%1").arg(filePath);
            closePort();
            return false;
        }

        fileSizes.append(entry.sizeInBytes > 0 ? entry.sizeInBytes : size);
        totalBytes += fileSizes.last();
    }

    qint64 completedBytes = 0;

    for(int index = 0; index < entries.size(); ++index) {
        if(m_cancel.loadRelaxed()) {
            if(error) *error = QStringLiteral("操作已取消");
            closePort();
            return false;
        }

        const int startPercent = 10 + static_cast<int>((85.0 * completedBytes) / qMax<qint64>(1, totalBytes));
        const int endPercent = 10 + static_cast<int>((85.0 * (completedBytes + fileSizes.at(index)) /
                                                      qMax<qint64>(1, totalBytes)));

        ProgressFn entryProgress;

        if(progress) {
            entryProgress = [progress, startPercent, endPercent](int percent) {
                const int mapped = startPercent + ((endPercent - startPercent) * percent) / 100;
                progress(qMin(95, mapped));
            };
        }

        if(log) {
            log(QStringLiteral("[%1/%2] %3").arg(index + 1).arg(entries.size()).arg(entries.at(index).label));
        }

        if(!programEntry(entries.at(index), baseDir, maxPayload, entryProgress, log, error)) {
            closePort();
            return false;
        }

        completedBytes += fileSizes.at(index);
    }

    QString resetError;

    if(!powerReset(log, &resetError)) {
        if(log) {
            log(QStringLiteral("重启命令失败（可手动拔插设备）：%1").arg(resetError));
        }
    }

    if(progress) {
        progress(100);
    }

    closePort();

    return true;
}

quint32 EdlClient::readLe32(const QByteArray &data, int offset)
{
    if(offset < 0 || offset + 4 > data.size()) {
        return 0;
    }

    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()) + offset);
}

quint64 EdlClient::readLe64(const QByteArray &data, int offset)
{
    if(offset < 0 || offset + 8 > data.size()) {
        return 0;
    }

    return qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(data.constData()) + offset);
}

void EdlClient::writeLe32(QByteArray &data, int offset, quint32 value)
{
    if(offset + 4 > data.size()) {
        data.resize(offset + 4);
    }

    qToLittleEndian<quint32>(value, reinterpret_cast<uchar*>(data.data()) + offset);
}

bool EdlClient::xmlIsAck(const QByteArray &response)
{
    return response.contains("value=\"ACK\"") || response.contains("value='ACK'");
}

QString EdlClient::xmlAttribute(const QByteArray &response, const QString &name)
{
    const QRegularExpression pattern(QStringLiteral("%1=\"([^\"]*)\"").arg(QRegularExpression::escape(name)));
    const auto match = pattern.match(QString::fromUtf8(response));

    return match.hasMatch() ? match.captured(1) : QString();
}

QString EdlClient::xmlLogs(const QByteArray &response)
{
    const QRegularExpression pattern(QStringLiteral("<log value=\"([^\"]*)\""));
    QStringList messages;
    auto iterator = pattern.globalMatch(QString::fromUtf8(response));

    while(iterator.hasNext()) {
        const auto match = iterator.next();
        QString text = match.captured(1);
        text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        messages.append(text.trimmed());
    }

    return messages.join(QStringLiteral("；"));
}

} // namespace AndroidFlasher
