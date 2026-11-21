#pragma once

// [ANDROID-FLASHER] 新增文件：安卓设备枚举器
// 通过 Windows SetupAPI 枚举 USB 设备节点，按接口类 / 关键字 / VID-PID 区分
//   普通安卓(ADB/MTP) / Fastboot / 高通 9008(QDL)
// 不依赖 qFlipper 原有的 Flipper USB 探测器，两套逻辑完全独立。

#include <QString>
#include <QStringList>
#include <QVector>

#include "androidtypes.h"

namespace AndroidFlasher {

class AndroidDeviceScanner
{
public:
    // 枚举当前所有可见的安卓类设备（含 9008）
    static QVector<DeviceInfo> scan();

    // 返回当前 9008 设备的串口名（COMx），没有则返回空字符串
    static QString findQualcommPort();

    // 当前系统可见的串口列表，用于重连时做有效性判断
    static QStringList availableSerialPorts();
};

} // namespace AndroidFlasher
