#include "androiddevicescanner.h"

// [ANDROID-FLASHER] 新增文件实现：Windows SetupAPI 枚举 + 设备分类

#include <QRegularExpression>
#include <QSerialPortInfo>

#ifdef Q_OS_WIN

#include <windows.h>
#include <setupapi.h>
#include <devpkey.h>

namespace {

// 直接写死接口 GUID，避免不同 Windows SDK 头文件差异
// {A5DCBF10-6530-11D2-901F-00C04FB951ED} = GUID_DEVINTERFACE_USB_DEVICE
const GUID USB_DEVICE_INTERFACE_GUID =
    {0xa5dcbf10, 0x6530, 0x11d2, {0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed}};

QString readRegistryString(HDEVINFO set, SP_DEVINFO_DATA *info, DWORD property)
{
    wchar_t buffer[2048];
    DWORD type = 0;
    DWORD required = 0;

    if(!SetupDiGetDeviceRegistryPropertyW(set, info, property, &type,
                                          reinterpret_cast<PBYTE>(buffer),
                                          sizeof(buffer), &required)) {
        return QString();
    }

    if(type == REG_MULTI_SZ) {
        // 多字符串：用 \0 分隔，转成便于正则匹配的一整串
        const int count = static_cast<int>(required / sizeof(wchar_t));
        QString joined;

        for(int i = 0; i < count; ++i) {
            if(buffer[i] == L'\0') {
                if(i + 1 < count && buffer[i + 1] == L'\0') {
                    break;
                }
                joined.append(QLatin1Char('\n'));
            } else {
                joined.append(QChar(buffer[i]));
            }
        }

        return joined;
    }

    return QString::fromWCharArray(buffer);
}

// [ANDROID-FLASHER] MinGW 的 devpkey.h 没有 DEVPKEY_Device_BusReportedDeviceDesc 的实体定义，
// 按官方 GUID/PID 本地声明一份，MSVC / MinGW 都能用。
static const DEVPROPKEY kBusReportedDescKey = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } },
    4
};

QString readBusReportedDescription(HDEVINFO set, SP_DEVINFO_DATA *info)
{
    wchar_t buffer[1024];
    DEVPROPTYPE type = 0;
    DWORD size = 0;

    if(!SetupDiGetDevicePropertyW(set, info,
                                  &kBusReportedDescKey, &type,
                                  reinterpret_cast<PBYTE>(buffer),
                                  sizeof(buffer), &size, 0)) {
        return QString();
    }

    if(type != DEVPROP_TYPE_STRING) {
        return QString();
    }

    return QString::fromWCharArray(buffer);
}

QString deviceInterfacePath(HDEVINFO set, SP_DEVINFO_DATA *info, const GUID &guid)
{
    SP_DEVICE_INTERFACE_DATA interfaceData;
    ZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.cbSize = sizeof(interfaceData);

    if(!SetupDiEnumDeviceInterfaces(set, info, &guid, 0, &interfaceData)) {
        return QString();
    }

    DWORD required = 0;
    SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, nullptr, 0, &required, nullptr);

    if(GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0) {
        return QString();
    }

    auto *detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(LocalAlloc(LMEM_FIXED, required));

    if(!detail) {
        return QString();
    }

    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    QString path;

    if(SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, detail, required, nullptr, nullptr)) {
        path = QString::fromWCharArray(detail->DevicePath);
    }

    LocalFree(detail);

    return path;
}

QString parseComPort(const QString &friendlyName)
{
    static const QRegularExpression comPattern(QStringLiteral("\\((COM\\d+)\\)"),
                                               QRegularExpression::CaseInsensitiveOption);

    const auto match = comPattern.match(friendlyName);

    return match.hasMatch() ? match.captured(1).toUpper() : QString();
}

QString parsePortFromRegistry(HDEVINFO set, SP_DEVINFO_DATA *info)
{
    HKEY key = SetupDiOpenDevRegKey(set, info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);

    if(key == INVALID_HANDLE_VALUE) {
        return QString();
    }

    wchar_t buffer[256];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    QString result;

    if(RegQueryValueExW(key, L"PortName", nullptr, &type,
                        reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS && type == REG_SZ) {
        result = QString::fromWCharArray(buffer);
    }

    RegCloseKey(key);

    return result;
}

bool parseVidPid(const QString &instanceId, int *vid, int *pid)
{
    static const QRegularExpression vidPidPattern(
        QStringLiteral("VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = vidPidPattern.match(instanceId);

    if(!match.hasMatch()) {
        return false;
    }

    *vid = match.captured(1).toInt(nullptr, 16);
    *pid = match.captured(2).toInt(nullptr, 16);

    return true;
}

QString parseSerial(const QString &instanceId)
{
    const int index = instanceId.lastIndexOf(QLatin1Char('\\'));

    return index >= 0 ? instanceId.mid(index + 1) : QString();
}

struct InterfaceClass {
    int cls = -1;
    int sub = -1;
    int proto = -1;
};

InterfaceClass parseInterfaceClass(const QString &compatibleIds)
{
    static const QRegularExpression classPattern(
        QStringLiteral("Class_([0-9A-Fa-f]{2})&SubClass_([0-9A-Fa-f]{2})&Prot_([0-9A-Fa-f]{2})"),
        QRegularExpression::CaseInsensitiveOption);

    InterfaceClass result;
    auto iterator = classPattern.globalMatch(compatibleIds);

    while(iterator.hasNext()) {
        const auto match = iterator.next();
        const int cls = match.captured(1).toInt(nullptr, 16);
        const int sub = match.captured(2).toInt(nullptr, 16);
        const int proto = match.captured(3).toInt(nullptr, 16);

        // 安卓厂商自定义接口：Class 0xFF / SubClass 0x42
        if(cls == 0xFF && sub == 0x42) {
            result.cls = cls;
            result.sub = sub;
            result.proto = proto;
            return result;
        }

        // MTP：Class 0x06 / SubClass 0x01 / Prot 0x01
        if(cls == 0x06 && sub == 0x01) {
            result.cls = cls;
            result.sub = sub;
            result.proto = proto;
        }
    }

    return result;
}

QString cleanProductName(const QString &name)
{
    QString result = name;

    result.remove(QRegularExpression(QStringLiteral("\\s*\\(COM\\d+\\)"), QRegularExpression::CaseInsensitiveOption));
    result.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    result = result.trimmed();

    return result;
}

} // namespace

#endif // Q_OS_WIN

namespace AndroidFlasher {

QVector<DeviceInfo> AndroidDeviceScanner::scan()
{
    QVector<DeviceInfo> result;

#ifdef Q_OS_WIN
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                        DIGCF_PRESENT | DIGCF_ALLCLASSES);

    if(set == INVALID_HANDLE_VALUE) {
        return result;
    }

    SP_DEVINFO_DATA info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);

    for(DWORD index = 0; SetupDiEnumDeviceInfo(set, index, &info); ++index) {
        wchar_t instanceBuffer[512];
        DWORD required = 0;

        if(!SetupDiGetDeviceInstanceIdW(set, &info, instanceBuffer,
                                        sizeof(instanceBuffer) / sizeof(wchar_t), &required)) {
            continue;
        }

        const QString instanceId = QString::fromWCharArray(instanceBuffer);

        int vid = 0;
        int pid = 0;

        if(!parseVidPid(instanceId, &vid, &pid)) {
            continue;
        }

        const QString friendlyName = readRegistryString(set, &info, SPDRP_FRIENDLYNAME);
        const QString deviceDescription = readRegistryString(set, &info, SPDRP_DEVICEDESC);
        const QString manufacturer = readRegistryString(set, &info, SPDRP_MFG);
        const QString compatibleIds = readRegistryString(set, &info, SPDRP_COMPATIBLEIDS);
        const QString busDescription = readBusReportedDescription(set, &info);

        DeviceInfo device;
        device.instanceId = instanceId;
        device.serial = parseSerial(instanceId);
        device.vid = vid;
        device.pid = pid;
        device.manufacturer = manufacturer;
        device.detail = friendlyName.isEmpty() ? deviceDescription : friendlyName;
        device.product = cleanProductName(!busDescription.isEmpty() ? busDescription :
                                          (!friendlyName.isEmpty() ? friendlyName : deviceDescription));

        const QString searchText = (friendlyName + QLatin1Char(' ') + deviceDescription +
                                    QLatin1Char(' ') + busDescription).toLower();

        DeviceMode appleMode = DeviceMode::Unknown;

        if(isAppleDevice(vid, pid, &appleMode)) {
            // [ANDROID-FLASHER] iOS 适配：识别苹果设备处于 DFU / 恢复模式 / 正常模式。
            // 刷写仍需 iTunes 或 idevicerestore，本程序只做识别与信息展示。
            device.mode = appleMode;
            device.port = QStringLiteral("USB");
            device.path = deviceInterfacePath(set, &info, USB_DEVICE_INTERFACE_GUID);

            if(device.product.isEmpty()) {
                device.product = QStringLiteral("iPhone / iPad");
            }

            // [ANDROID-FLASHER] iOS 机型增强：DFU / 恢复模式的序列号里带 CPID，
            // 能读出就能显示芯片代次（A11/A12/A13…），识别不出就保持原样。
            int cpid = 0;

            if(!appleParseCpid(device.serial, &cpid)) {
                // DFU 序列号被驱动改写时，再从实例 ID 原文里捞一次
                appleParseCpid(instanceId, &cpid);
            }

            if(cpid > 0) {
                const QString chip = appleChipName(cpid);

                if(!chip.isEmpty()) {
                    device.product = QStringLiteral("%1 · %2 (CPID 0x%3)")
                                     .arg(device.product, chip,
                                          QStringLiteral("%1").arg(cpid, 4, 16, QLatin1Char('0')));
                }

                const QString modeText =
                    appleMode == DeviceMode::IosDfu ? QStringLiteral("DFU") :
                    appleMode == DeviceMode::IosRecovery ? QStringLiteral("Recovery 恢复模式") :
                    QStringLiteral("Normal 正常模式");

                device.detail = device.detail.isEmpty() ?
                                modeText :
                                device.detail + QStringLiteral(" / ") + modeText;
            }
        } else if(isQualcommLoaderId(vid, pid) ||
           (vid == 0x05C6 && searchText.contains(QStringLiteral("qdloader")))) {
            device.mode = DeviceMode::Qdl;
            device.port = parseComPort(friendlyName);

            if(device.port.isEmpty()) {
                device.port = parsePortFromRegistry(set, &info);
            }

            if(device.product.isEmpty()) {
                device.product = QStringLiteral("Qualcomm 9008");
            }
        } else {
            const InterfaceClass interfaceClass = parseInterfaceClass(compatibleIds);

            if(interfaceClass.cls == 0xFF && interfaceClass.sub == 0x42 &&
               interfaceClass.proto == 0x01) {
                device.mode = DeviceMode::Adb;
            } else if(interfaceClass.cls == 0xFF && interfaceClass.sub == 0x42 &&
                       interfaceClass.proto == 0x03) {
                device.mode = DeviceMode::Fastboot;
            } else if(interfaceClass.cls == 0x06 && isKnownAndroidVendor(vid)) {
                device.mode = DeviceMode::Adb;
                device.mtp = true;
            } else if(searchText.contains(QStringLiteral("fastboot")) ||
                       searchText.contains(QStringLiteral("bootloader"))) {
                device.mode = DeviceMode::Fastboot;
            } else if(searchText.contains(QStringLiteral("adb")) ||
                       (searchText.contains(QStringLiteral("android")) && isKnownAndroidVendor(vid))) {
                device.mode = DeviceMode::Adb;
            } else {
                // 无法归类的 USB 设备直接忽略，避免误报
                continue;
            }

            device.port = QStringLiteral("USB");
            device.path = deviceInterfacePath(set, &info, USB_DEVICE_INTERFACE_GUID);

            if(device.path.isEmpty()) {
                // 设备存在，但当前驱动不是 WinUSB（例如原厂 MTP 驱动）。
                // 这种情况仍展示设备信息，但刷机操作会给出明确提示。
                device.detail = device.detail.isEmpty() ?
                                QStringLiteral("未找到 WinUSB 接口") :
                                device.detail + QStringLiteral("（非 WinUSB 驱动）");
            }
        }

        if(device.mode == DeviceMode::Qdl) {
            device.path = deviceInterfacePath(set, &info, USB_DEVICE_INTERFACE_GUID);
        }

        result.append(device);
    }

    SetupDiDestroyDeviceInfoList(set);

    // 同一台手机同时暴露 MTP 与 ADB 两个接口时，只保留能力更强的那个
    for(int i = result.size() - 1; i >= 0; --i) {
        if(!result.at(i).mtp) {
            continue;
        }

        bool hasBetterInterface = false;

        for(int j = 0; j < result.size(); ++j) {
            if(i == j) {
                continue;
            }

            if(result.at(j).vid == result.at(i).vid && result.at(j).pid == result.at(i).pid &&
               !result.at(j).mtp) {
                hasBetterInterface = true;
                break;
            }
        }

        if(hasBetterInterface) {
            result.remove(i);
        }
    }
#endif // Q_OS_WIN

    return result;
}

QString AndroidDeviceScanner::findQualcommPort()
{
    const auto devices = scan();

    for(const auto &device : devices) {
        if(device.mode == DeviceMode::Qdl && !device.port.isEmpty()) {
            return device.port;
        }
    }

    return QString();
}

QStringList AndroidDeviceScanner::availableSerialPorts()
{
    QStringList ports;

    for(const auto &info : QSerialPortInfo::availablePorts()) {
        ports.append(info.portName());
    }

    return ports;
}

} // namespace AndroidFlasher
