#pragma once

// [ANDROID-FLASHER] 新增文件：rawprogram.xml 解析
// 只解析 QFIL/MiFlash 固件包中的 rawprogram*.xml，提取 <program> 条目。

#include <QString>
#include <QVector>

namespace AndroidFlasher {

struct RawProgramEntry {
    QString label;              // 分区名，例如 boot / system
    QString filename;           // 镜像文件名（相对 xml 所在目录）
    int physicalPartition = 0;  // 物理分区号（UFS/eMMC LUN）
    qint64 startSector = 0;     // 起始扇区
    qint64 numSectors = 0;      // 占用扇区数
    qint64 sizeInBytes = 0;     // 声明的大小（可能为 0）
    int sectorSize = 512;       // 该条目的扇区大小
};

class RawProgramParser
{
public:
    static bool parse(const QString &xmlPath, QVector<RawProgramEntry> *entries, QString *error);
};

} // namespace AndroidFlasher
