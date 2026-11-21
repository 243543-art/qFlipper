#pragma once

// [ANDROID-FLASHER] 新增文件：安卓刷机工作线程对象
// 所有阻塞式 USB/串口操作都在这个对象里执行（由 AndroidBackend 投递到独立 QThread），
// 保证 Qt 主线程（QML 界面）不会被刷机过程卡住。

#include <QAtomicInt>
#include <QObject>
#include <QString>

namespace AndroidFlasher {

class AndroidWorker : public QObject
{
    Q_OBJECT

public:
    explicit AndroidWorker(QObject *parent = nullptr);

public slots:
    void cancel();

    // [ANDROID-FLASHER] 语言开关：由 AndroidBackend 在主线程投递过来
    void setEnglish(bool english);

    void fastbootFlash(const QString &devicePath, const QString &partition, const QString &filePath);
    void fastbootErase(const QString &devicePath, const QString &partition);
    void fastbootReboot(const QString &devicePath, const QString &target);
    void fastbootQuery(const QString &devicePath);

    void edlFlash(const QString &portName, const QString &rawProgramPath,
                  const QString &programmerPath);

signals:
    void progressChanged(int percent);
    void logMessage(const QString &message);
    void operationFinished(bool success, const QString &message);
    void deviceVarsReady(const QString &devicePath, const QString &product, const QString &serial);

private:
    QString loc(const QString &chinese, const QString &english) const;

    QAtomicInt m_cancel;
    bool m_english = false;
};

} // namespace AndroidFlasher
