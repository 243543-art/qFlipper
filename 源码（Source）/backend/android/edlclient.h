#pragma once

// [ANDROID-FLASHER] 新增文件：高通 9008 (EDL) 客户端
// 通过 Qualcomm HS-USB QDLoader 9008 的虚拟串口，实现：
//   Sahara（上传 firehose programmer） + Firehose（按 rawprogram.xml 烧录）
// 参考协议：Qualcomm Sahara v2 / Firehose 2.0（与 QFIL、bkerler/edl 行为一致）

#include <QByteArray>
#include <QSerialPort>
#include <QString>
#include <QVector>

#include <QAtomicInt>
#include <functional>

#include "rawprogramparser.h"

namespace AndroidFlasher {

class EdlClient
{
public:
    using ProgressFn = std::function<void(int)>;
    using LogFn = std::function<void(const QString &)>;

    EdlClient();
    ~EdlClient();

    bool flash(const QString &portName,
               const QString &loaderPath,
               const QVector<RawProgramEntry> &entries,
               const QString &baseDir,
               const ProgressFn &progress,
               const LogFn &log,
               QString *error);

    void cancel();

private:
    bool openPort(const QString &portName, QString *error);
    void closePort();
    bool waitForFirehosePort(const LogFn &log, QString *error);

    bool handshakeSahara(const LogFn &log, QString *error);
    bool uploadLoader(const QString &loaderPath, const ProgressFn &progress,
                      const LogFn &log, QString *error);
    bool configure(int *maxPayload, const LogFn &log, QString *error);
    bool programEntry(const RawProgramEntry &entry, const QString &baseDir, int maxPayload,
                      const ProgressFn &progress, const LogFn &log, QString *error);
    bool powerReset(const LogFn &log, QString *error);

    bool readSaharaPacket(quint32 *command, QByteArray *payload, int timeoutMs, QString *error);
    bool readExact(QByteArray *out, int count, int timeoutMs, QString *error);
    bool writeRaw(const QByteArray &data, int timeoutMs, QString *error);
    QByteArray readFirehoseResponse(int timeoutMs);
    bool sendXml(const QString &xml, QByteArray *response, QString *error, int timeoutMs = 15000);

    static quint32 readLe32(const QByteArray &data, int offset);
    static quint64 readLe64(const QByteArray &data, int offset);
    static void writeLe32(QByteArray &data, int offset, quint32 value);
    static bool xmlIsAck(const QByteArray &response);
    static QString xmlAttribute(const QByteArray &response, const QString &name);
    static QString xmlLogs(const QByteArray &response);

    QSerialPort m_port;
    QString m_portName;
    QByteArray m_rxBuffer;
    bool m_firehoseMode = false;
    QAtomicInt m_cancel;
};

} // namespace AndroidFlasher
