#pragma once

// [ANDROID-FLASHER] 新增文件：安卓设备通用类型定义
// 说明：本文件只放纯数据结构与轻量工具函数，不依赖任何 UI/平台代码，
//      便于 Flipper 原业务与安卓业务共用而不互相污染。

#include <QString>
#include <QVector>

namespace AndroidFlasher {

enum class DeviceMode {
    Unknown = 0,
    Adb,        // 普通安卓（USB 调试 / ADB 接口）
    Fastboot,   // Bootloader / Fastboot 模式
    Qdl,        // 高通 9008 (Qualcomm HS-USB QDLoader)
    IosDfu,     // 苹果 DFU（14 号复合设备，仅识别）
    IosRecovery,// 苹果恢复模式（iBoot，仅识别）
    IosNormal   // 苹果正常模式（仅识别）
};

struct DeviceInfo {
    QString path;           // SetupAPI 设备接口路径，WinUSB 打开时需要
    QString instanceId;     // 例如 USB\VID_2717&PID_FF48\xxxx
    QString serial;         // 设备序列号（取自实例 ID 末段）
    QString port;           // QDL: "COM3"；其它: "USB"
    QString product;        // 型号（USB 描述，Fastboot 会用 getvar 补全）
    QString manufacturer;   // 厂商字符串
    QString detail;         // Windows 友好名称，便于排查
    int vid = 0;
    int pid = 0;
    DeviceMode mode = DeviceMode::Unknown;
    bool mtp = false;       // 仅 MTP 接口，没有 ADB/Fastboot 能力（仅用于界面提示）

    QString vidPid() const {
        return QStringLiteral("%1:%2")
                .arg(vid, 4, 16, QLatin1Char('0'))
                .arg(pid, 4, 16, QLatin1Char('0'))
                .toUpper();
    }

    // 设备唯一键：优先用 WinUSB 接口路径；没有 WinUSB 时退回实例 ID，
    // 保证 MTP/无驱动设备也能被选中并展示信息。
    QString key() const {
        return path.isEmpty() ? instanceId : path;
    }

    bool canFastboot() const { return mode == DeviceMode::Fastboot; }
    bool canEdl() const { return mode == DeviceMode::Qdl; }
    bool isIos() const {
        return mode == DeviceMode::IosDfu
            || mode == DeviceMode::IosRecovery
            || mode == DeviceMode::IosNormal;
    }
};

inline QString modeName(DeviceMode mode)
{
    switch(mode) {
    case DeviceMode::Adb:         return QStringLiteral("普通安卓 (ADB)");
    case DeviceMode::Fastboot:    return QStringLiteral("Fastboot");
    case DeviceMode::Qdl:         return QStringLiteral("高通 9008 (QDL)");
    case DeviceMode::IosDfu:      return QStringLiteral("iOS DFU");
    case DeviceMode::IosRecovery: return QStringLiteral("iOS 恢复模式");
    case DeviceMode::IosNormal:   return QStringLiteral("iOS 设备");
    default:                      return QStringLiteral("未知");
    }
}

inline QString modeNameEn(DeviceMode mode)
{
    switch(mode) {
    case DeviceMode::Adb:         return QStringLiteral("Android (ADB)");
    case DeviceMode::Fastboot:    return QStringLiteral("Fastboot");
    case DeviceMode::Qdl:         return QStringLiteral("Qualcomm 9008 (QDL)");
    case DeviceMode::IosDfu:      return QStringLiteral("iOS DFU");
    case DeviceMode::IosRecovery: return QStringLiteral("iOS Recovery");
    case DeviceMode::IosNormal:   return QStringLiteral("iOS Device");
    default:                      return QStringLiteral("Unknown");
    }
}

inline QString modeKey(DeviceMode mode)
{
    switch(mode) {
    case DeviceMode::Adb:         return QStringLiteral("adb");
    case DeviceMode::Fastboot:    return QStringLiteral("fastboot");
    case DeviceMode::Qdl:         return QStringLiteral("qdl");
    case DeviceMode::IosDfu:      return QStringLiteral("iosdfu");
    case DeviceMode::IosRecovery: return QStringLiteral("iosrecovery");
    case DeviceMode::IosNormal:   return QStringLiteral("ios");
    default:                      return QStringLiteral("unknown");
    }
}

// 苹果 USB 设备（VID 0x05AC）：只做识别与信息展示，刷机仍需 iTunes / idevicerestore
inline bool isAppleDevice(int vid, int pid, DeviceMode *mode = nullptr)
{
    if(vid != 0x05AC) {
        return false;
    }

    DeviceMode result = DeviceMode::Unknown;

    switch(pid) {
    case 0x1227:                        // Apple Mobile Device (DFU Mode)
    case 0x1222:                        // WTF 模式（DFU 变体）
        result = DeviceMode::IosDfu;
        break;
    case 0x1280:                        // Recovery Mode (A5)
    case 0x1281:                        // Recovery Mode
    case 0x1283:                        // Recovery Mode (A7/A8)
        result = DeviceMode::IosRecovery;
        break;
    case 0x12A8:                        // 正常模式（已信任/已连接）
    case 0x12AB:                        // 早期机型正常模式
        result = DeviceMode::IosNormal;
        break;
    default:
        result = DeviceMode::IosNormal;  // 苹果 VID 的其它 PID 一律按 iOS 设备列出
        break;
    }

    if(mode) {
        *mode = result;
    }

    return true;
}

// [ANDROID-FLASHER] 苹果芯片代号（CPID）→ 常见芯片名，仅用于界面显示。
// 说明：苹果 DFU / 恢复模式的 USB 序列号里带 CPID，能读到就能告诉用户这是哪一代芯片，
// 本程序不去做任何越狱、签名或刷机判断，只显示。
inline QString appleChipName(int cpid)
{
    switch(cpid) {
    case 0x8010: return QStringLiteral("A10");
    case 0x8011: return QStringLiteral("A10X");
    case 0x8015: return QStringLiteral("A11");
    case 0x8020: return QStringLiteral("A12");
    case 0x8027: return QStringLiteral("A13");
    case 0x8030: return QStringLiteral("A14");
    case 0x8101: return QStringLiteral("A15");
    case 0x8110: return QStringLiteral("A16");
    case 0x8120: return QStringLiteral("A17 Pro");
    default:     return QString();
    }
}

// 从 "CPID:8015 CPRV:11 ECID:..." 这类序列号里取出 CPID（十六进制）
inline bool appleParseCpid(const QString &serial, int *cpid)
{
    if(serial.isEmpty()) {
        return false;
    }

    const int index = serial.indexOf(QStringLiteral("CPID"), 0, Qt::CaseInsensitive);

    if(index < 0) {
        return false;
    }

    // 后面可能紧跟 ':'、'&'、'_' 等分隔符，也可能被 Windows 转义成 "CPID&3a8015"
    QString digits;
    const QString tail = serial.mid(index + 4, 12).toUpper();

    for(const QChar ch : tail) {
        if(digits.size() >= 4) {
            break;
        }

        if((ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
           (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))) {
            digits.append(ch);
        } else if(!digits.isEmpty()) {
            break;
        }
    }

    if(digits.size() != 4) {
        return false;
    }

    bool ok = false;
    const int value = digits.toInt(&ok, 16);

    if(!ok || value <= 0) {
        return false;
    }

    if(cpid) {
        *cpid = value;
    }

    return true;
}

// 机型档案：用于 fastboot 刷机前的型号校验与界面显示。
// codename 为空表示"只做标识、不做校验"（避免把没核实过的代号写死成规则）。
struct ModelProfile {
    QString label;      // 界面显示名
    QString codename;   // fastboot getvar product 返回的代号
};

inline QVector<ModelProfile> modelProfiles()
{
    QVector<ModelProfile> list;

    list.append({ QStringLiteral("红米 Note 5"),       QStringLiteral("whyred") });
    list.append({ QStringLiteral("红米 Note 8"),       QStringLiteral("ginkgo") });
    list.append({ QStringLiteral("红米 K40"),          QStringLiteral("alioth") });
    list.append({ QStringLiteral("红米 Note 12 Turbo"),QStringLiteral("marble") });
    list.append({ QStringLiteral("小米 9"),            QStringLiteral("cepheus") });
    list.append({ QStringLiteral("小米 10"),           QStringLiteral("umi") });
    list.append({ QStringLiteral("小米 11"),           QStringLiteral("venus") });
    list.append({ QStringLiteral("一加 9"),            QStringLiteral("lemonade") });
    list.append({ QStringLiteral("Google Pixel 7"),    QStringLiteral("panther") });
    list.append({ QStringLiteral("Google Pixel 4"),    QStringLiteral("flame") });
    list.append({ QStringLiteral("荣耀 30 Pro"),       QStringLiteral("") });
    list.append({ QStringLiteral("华为 P40"),          QStringLiteral("") });
    list.append({ QStringLiteral("三星 Galaxy S20"),   QStringLiteral("") });
    list.append({ QStringLiteral("其它 / 不校验"),     QStringLiteral("") });

    return list;
}


// 高通 EDL/下载模式常见 PID（9008 是最常见的“紧急下载”模式）
inline bool isQualcommLoaderId(int vid, int pid)
{
    if(vid != 0x05C6) {
        return false;
    }

    switch(pid) {
    case 0x9008:    // HS-USB QDLoader 9008
    case 0x900E:    // HS-USB QDLoader 9008 (部分机型/复合设备)
    case 0x901D:    // Sahara / Firehose 复合接口
    case 0x9024:    // QDLoader (少数平台)
    case 0x9091:    // 部分高通平台下载口
        return true;
    default:
        return false;
    }
}

// 常见安卓厂商 VID。仅用于“兼容 ID 缺失时”的兜底判断，避免误报。
inline bool isKnownAndroidVendor(int vid)
{
    switch(vid) {
    case 0x18D1:    // Google
    case 0x04E8:    // Samsung
    case 0x2717:    // Xiaomi
    case 0x22B8:    // Motorola
    case 0x12D1:    // Huawei
    case 0x0FCE:    // Sony
    case 0x0BB4:    // HTC
    case 0x1004:    // LG
    case 0x19D2:    // ZTE
    case 0x2A70:    // OnePlus
    case 0x22D9:    // OPPO
    case 0x2D95:    // vivo
    case 0x0B05:    // ASUS
    case 0x413C:    // Dell
    case 0x2916:    // 联想/摩托罗拉部分机型
    case 0x05C6:    // Qualcomm 参考设备
    case 0x0E8D:    // MediaTek 参考设备
        return true;
    default:
        return false;
    }
}

} // namespace AndroidFlasher
