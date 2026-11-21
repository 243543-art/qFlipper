#include "rawprogramparser.h"

// [ANDROID-FLASHER] 新增文件实现：rawprogram.xml 解析

#include <QFile>
#include <QXmlStreamReader>

namespace AndroidFlasher {

namespace {

bool readNumber(const QString &text, qint64 *value)
{
    const QString trimmed = text.trimmed();

    if(trimmed.isEmpty()) {
        return false;
    }

    bool ok = false;
    const qint64 parsed = trimmed.toLongLong(&ok, 0);

    if(!ok) {
        return false;
    }

    *value = parsed;

    return true;
}

} // namespace

bool RawProgramParser::parse(const QString &xmlPath, QVector<RawProgramEntry> *entries, QString *error)
{
    QFile file(xmlPath);

    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if(error) *error = QStringLiteral("无法打开 rawprogram 文件：%1").arg(xmlPath);
        return false;
    }

    QXmlStreamReader reader(&file);
    QVector<RawProgramEntry> parsedEntries;

    while(!reader.atEnd()) {
        reader.readNext();

        if(!reader.isStartElement()) {
            continue;
        }

        if(reader.name() != QLatin1String("program")) {
            continue;
        }

        const QXmlStreamAttributes attributes = reader.attributes();

        auto attribute = [&attributes](const char *name) {
            return attributes.value(QLatin1String(name)).toString();
        };

        RawProgramEntry entry;
        entry.label = attribute("label");
        entry.filename = attribute("filename").trimmed();

        if(entry.filename.isEmpty()) {
            // 有些固件包会用空 filename 表示“保留/跳过”，直接忽略
            continue;
        }

        entry.physicalPartition = attribute("physical_partition_number").toInt();

        const QString sectorSizeText = attribute("SECTOR_SIZE_IN_BYTES");
        const int sectorSize = sectorSizeText.toInt();

        if(sectorSize > 0) {
            entry.sectorSize = sectorSize;
        }

        qint64 number = 0;

        if(readNumber(attribute("start_sector"), &number)) {
            entry.startSector = number;
        } else {
            if(error) {
                *error = QStringLiteral("条目 %1 的 start_sector 不是有效数字：%2")
                        .arg(entry.label.isEmpty() ? entry.filename : entry.label,
                             attribute("start_sector"));
            }
            return false;
        }

        if(readNumber(attribute("num_partition_sectors"), &number)) {
            entry.numSectors = number;
        }

        if(readNumber(attribute("size_in_bytes"), &number)) {
            entry.sizeInBytes = number;
        }

        if(entry.label.isEmpty()) {
            entry.label = entry.filename;
        }

        parsedEntries.append(entry);
    }

    if(reader.hasError()) {
        if(error) *error = QStringLiteral("解析 rawprogram 失败（第 %1 行）：%2")
                .arg(reader.lineNumber())
                .arg(reader.errorString());
        return false;
    }

    if(parsedEntries.isEmpty()) {
        if(error) *error = QStringLiteral("该 rawprogram 文件中没有任何 <program> 条目");
        return false;
    }

    if(entries) {
        *entries = parsedEntries;
    }

    return true;
}

} // namespace AndroidFlasher
