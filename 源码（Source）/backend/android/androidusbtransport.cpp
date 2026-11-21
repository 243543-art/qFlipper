#include "androidusbtransport.h"

// [ANDROID-FLASHER] 新增文件实现：WinUSB 批量传输
// 仅 Windows 下有效；其它平台编译成“不可用”的空实现，保证 qFlipper 仍能构建。

#include <QByteArray>

#ifdef Q_OS_WIN

#include <windows.h>
#include <winusb.h>
#include <usb.h>

namespace {

// USBD_PIPE_TYPE::UsbdPipeTypeBulk 的数值。
// 这里用常量而不是包含 usb.h，避免和 usbspec.h 中的描述符结构重复定义。
const int USBD_PIPE_TYPE_BULK_VALUE = 2;

QString winErrorText(DWORD code)
{
    wchar_t buffer[512];

    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, buffer, 512, nullptr);
    QString text = length ? QString::fromWCharArray(buffer).trimmed() : QString();

    if(text.isEmpty()) {
        text = QStringLiteral("WinError %1").arg(code);
    }

    return text;
}

} // namespace

namespace AndroidFlasher {

struct AndroidUsbTransport::Private
{
    HANDLE file = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE usb = nullptr;
    UCHAR inPipe = 0;
    UCHAR outPipe = 0;
    bool open = false;
};

AndroidUsbTransport::AndroidUsbTransport():
    d(new Private)
{}

AndroidUsbTransport::~AndroidUsbTransport()
{
    close();
    delete d;
}

bool AndroidUsbTransport::open(const QString &devicePath, QString *error)
{
    close();

    if(devicePath.isEmpty()) {
        if(error) *error = QStringLiteral("设备路径为空");
        return false;
    }

    const std::wstring widePath = devicePath.toStdWString();

    d->file = CreateFileW(widePath.c_str(),
                          GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                          nullptr);

    if(d->file == INVALID_HANDLE_VALUE) {
        if(error) *error = QStringLiteral("打开设备失败：%1").arg(winErrorText(GetLastError()));
        return false;
    }

    if(!WinUsb_Initialize(d->file, &d->usb)) {
        if(error) *error = QStringLiteral("WinUSB 初始化失败：%1").arg(winErrorText(GetLastError()));
        CloseHandle(d->file);
        d->file = INVALID_HANDLE_VALUE;
        return false;
    }

    USB_INTERFACE_DESCRIPTOR descriptor;
    ZeroMemory(&descriptor, sizeof(descriptor));

    if(!WinUsb_QueryInterfaceSettings(d->usb, 0, &descriptor)) {
        if(error) *error = QStringLiteral("读取接口描述符失败：%1").arg(winErrorText(GetLastError()));
        close();
        return false;
    }

    if(descriptor.bAlternateSetting != 0) {
        WinUsb_SetCurrentAlternateSetting(d->usb, 0);
    }

    d->inPipe = 0;
    d->outPipe = 0;

    for(UCHAR i = 0; i < descriptor.bNumEndpoints; ++i) {
        WINUSB_PIPE_INFORMATION pipe;
        ZeroMemory(&pipe, sizeof(pipe));

        if(!WinUsb_QueryPipe(d->usb, 0, i, &pipe)) {
            continue;
        }

        if(static_cast<int>(pipe.PipeType) != USBD_PIPE_TYPE_BULK_VALUE) {
            continue;
        }

        if(pipe.PipeId & 0x80) {
            d->inPipe = pipe.PipeId;
        } else {
            d->outPipe = pipe.PipeId;
        }
    }

    if(d->inPipe == 0 || d->outPipe == 0) {
        if(error) *error = QStringLiteral("该接口没有可用的批量端点（IN=0x%1, OUT=0x%2）")
                .arg(d->inPipe, 2, 16, QLatin1Char('0'))
                .arg(d->outPipe, 2, 16, QLatin1Char('0'));
        close();
        return false;
    }

    d->open = true;
    resetPipes();

    return true;
}

void AndroidUsbTransport::close()
{
    if(d->usb) {
        WinUsb_Free(d->usb);
        d->usb = nullptr;
    }

    if(d->file != INVALID_HANDLE_VALUE) {
        CloseHandle(d->file);
        d->file = INVALID_HANDLE_VALUE;
    }

    d->open = false;
    d->inPipe = 0;
    d->outPipe = 0;
}

bool AndroidUsbTransport::isOpen() const
{
    return d->open;
}

void AndroidUsbTransport::resetPipes()
{
    if(!d->usb) {
        return;
    }

    if(d->inPipe) {
        WinUsb_AbortPipe(d->usb, d->inPipe);
        WinUsb_FlushPipe(d->usb, d->inPipe);
        WinUsb_ResetPipe(d->usb, d->inPipe);
    }

    if(d->outPipe) {
        WinUsb_AbortPipe(d->usb, d->outPipe);
        WinUsb_FlushPipe(d->usb, d->outPipe);
        WinUsb_ResetPipe(d->usb, d->outPipe);
    }
}

bool AndroidUsbTransport::write(const QByteArray &data, int timeoutMs, QString *error)
{
    if(!d->open) {
        if(error) *error = QStringLiteral("USB 设备未打开");
        return false;
    }

    int offset = 0;

    while(offset < data.size()) {
        // Qt5/Qt6 的 QByteArray::size() 返回类型不同，这里显式统一成 qint64 再比较
        const qint64 remain = static_cast<qint64>(data.size()) - offset;
        const int chunk = static_cast<int>(qMin<qint64>(remain, 65536));

        OVERLAPPED overlapped;
        ZeroMemory(&overlapped, sizeof(overlapped));
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        if(!overlapped.hEvent) {
            if(error) *error = QStringLiteral("创建传输事件失败：%1").arg(winErrorText(GetLastError()));
            return false;
        }

        DWORD transferred = 0;
        const BOOL started = WinUsb_WritePipe(d->usb, d->outPipe,
                                              reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData() + offset)),
                                              static_cast<ULONG>(chunk), &transferred, &overlapped);

        if(!started) {
            const DWORD lastError = GetLastError();

            if(lastError != ERROR_IO_PENDING) {
                CloseHandle(overlapped.hEvent);
                if(error) *error = QStringLiteral("写入 USB 失败：%1").arg(winErrorText(lastError));
                resetPipes();
                return false;
            }

            const DWORD waitResult = WaitForSingleObject(overlapped.hEvent, static_cast<DWORD>(timeoutMs));

            if(waitResult != WAIT_OBJECT_0) {
                CancelIoEx(d->file, &overlapped);
                WinUsb_AbortPipe(d->usb, d->outPipe);
                CloseHandle(overlapped.hEvent);
                if(error) *error = QStringLiteral("写入 USB 超时（%1 ms）").arg(timeoutMs);
                return false;
            }

            if(!WinUsb_GetOverlappedResult(d->usb, &overlapped, &transferred, FALSE)) {
                const DWORD lastError = GetLastError();
                CloseHandle(overlapped.hEvent);
                if(error) *error = QStringLiteral("写入 USB 完成失败：%1").arg(winErrorText(lastError));
                resetPipes();
                return false;
            }
        }

        CloseHandle(overlapped.hEvent);

        if(transferred == 0) {
            if(error) *error = QStringLiteral("USB 写入返回 0 字节");
            return false;
        }

        offset += static_cast<int>(transferred);
    }

    return true;
}

QByteArray AndroidUsbTransport::read(int maxSize, int timeoutMs, QString *error)
{
    QByteArray result;

    if(!d->open) {
        if(error) *error = QStringLiteral("USB 设备未打开");
        return result;
    }

    OVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if(!overlapped.hEvent) {
        if(error) *error = QStringLiteral("创建传输事件失败：%1").arg(winErrorText(GetLastError()));
        return result;
    }

    result.resize(maxSize);

    DWORD transferred = 0;
    const BOOL started = WinUsb_ReadPipe(d->usb, d->inPipe, reinterpret_cast<PUCHAR>(result.data()),
                                         static_cast<ULONG>(maxSize), &transferred, &overlapped);

    if(!started) {
        const DWORD lastError = GetLastError();

        if(lastError != ERROR_IO_PENDING) {
            CloseHandle(overlapped.hEvent);
            result.clear();
            if(error) *error = QStringLiteral("读取 USB 失败：%1").arg(winErrorText(lastError));
            resetPipes();
            return result;
        }

        const DWORD waitResult = WaitForSingleObject(overlapped.hEvent, static_cast<DWORD>(timeoutMs));

        if(waitResult != WAIT_OBJECT_0) {
            CancelIoEx(d->file, &overlapped);
            WinUsb_AbortPipe(d->usb, d->inPipe);
            CloseHandle(overlapped.hEvent);
            result.clear();
            if(error) *error = QStringLiteral("读取 USB 超时（%1 ms）").arg(timeoutMs);
            return result;
        }

        if(!WinUsb_GetOverlappedResult(d->usb, &overlapped, &transferred, FALSE)) {
            const DWORD lastError = GetLastError();
            CloseHandle(overlapped.hEvent);
            result.clear();
            if(error) *error = QStringLiteral("读取 USB 完成失败：%1").arg(winErrorText(lastError));
            resetPipes();
            return result;
        }
    }

    CloseHandle(overlapped.hEvent);
    result.truncate(static_cast<int>(transferred));

    if(result.isEmpty() && error) {
        *error = QStringLiteral("USB 读取返回 0 字节");
    }

    return result;
}

} // namespace AndroidFlasher

#else // !Q_OS_WIN

namespace AndroidFlasher {

struct AndroidUsbTransport::Private
{
    bool open = false;
};

AndroidUsbTransport::AndroidUsbTransport():
    d(new Private)
{}

AndroidUsbTransport::~AndroidUsbTransport()
{
    delete d;
}

bool AndroidUsbTransport::open(const QString &devicePath, QString *error)
{
    Q_UNUSED(devicePath)

    if(error) *error = QStringLiteral("当前平台暂不支持直接操作 USB 设备（仅 Windows）");
    return false;
}

void AndroidUsbTransport::close()
{
    d->open = false;
}

bool AndroidUsbTransport::isOpen() const
{
    return d->open;
}

bool AndroidUsbTransport::write(const QByteArray &data, int timeoutMs, QString *error)
{
    Q_UNUSED(data)
    Q_UNUSED(timeoutMs)

    if(error) *error = QStringLiteral("当前平台暂不支持 USB 批量写入");
    return false;
}

QByteArray AndroidUsbTransport::read(int maxSize, int timeoutMs, QString *error)
{
    Q_UNUSED(maxSize)
    Q_UNUSED(timeoutMs)

    if(error) *error = QStringLiteral("当前平台暂不支持 USB 批量读取");
    return QByteArray();
}

void AndroidUsbTransport::resetPipes()
{}

} // namespace AndroidFlasher

#endif // Q_OS_WIN
