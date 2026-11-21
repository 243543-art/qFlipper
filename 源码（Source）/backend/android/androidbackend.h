#pragma once

// [ANDROID-FLASHER] 新增文件：安卓刷机后端（QML 单例 AndroidBackend）
// 职责：定时扫描 USB 设备、维护设备模型与选中项、把刷机请求投递到工作线程。
// 与 Flipper 原有的 ApplicationBackend 完全解耦，互不影响。

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

#include "androiddevicemodel.h"
#include "androidtypes.h"
#include "rawprogramparser.h"
#include "androidworker.h"

class QThread;
class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

namespace AndroidFlasher {

class AndroidBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractListModel *deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY devicesChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY isBusyChanged)
    Q_PROPERTY(bool isQuerying READ isQuerying NOTIFY isQueryingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusTextChanged)
    Q_PROPERTY(QVariantMap selectedDevice READ selectedDevice NOTIFY selectedDeviceChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)

    // [ANDROID-FLASHER] 中英切换：QML 里用 AndroidBackend.english 决定显示哪套文案
    Q_PROPERTY(bool english READ english NOTIFY languageChanged)

    // [ANDROID-FLASHER] 机型档案：界面下拉框数据源 + 当前选择 + 与设备代号的匹配结果
    Q_PROPERTY(QVariantList modelProfiles READ modelProfiles CONSTANT)
    Q_PROPERTY(int profileIndex READ profileIndex NOTIFY profileChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY profileChanged)
    Q_PROPERTY(QString profileCodename READ profileCodename NOTIFY profileChanged)
    Q_PROPERTY(QString modelCheckText READ modelCheckText NOTIFY modelCheckChanged)
    Q_PROPERTY(bool modelMismatch READ modelMismatch NOTIFY modelCheckChanged)

    // [ANDROID-FLASHER] 补丁自动下载
    Q_PROPERTY(bool patchBusy READ patchBusy NOTIFY patchChanged)
    Q_PROPERTY(int patchProgress READ patchProgress NOTIFY patchChanged)
    Q_PROPERTY(QString patchStatus READ patchStatus NOTIFY patchChanged)

public:
    explicit AndroidBackend(QObject *parent = nullptr);
    ~AndroidBackend() override;

    QAbstractListModel *deviceModel() const;
    int deviceCount() const;
    bool isBusy() const;
    bool isQuerying() const;
    int progress() const;
    QString statusText() const;
    bool statusIsError() const;
    QVariantMap selectedDevice() const;
    QString logText() const;

    bool english() const;
    QVariantList modelProfiles() const;
    int profileIndex() const;
    QString profileName() const;
    QString profileCodename() const;
    QString modelCheckText() const;
    bool modelMismatch() const;

    bool patchBusy() const;
    int patchProgress() const;
    QString patchStatus() const;

    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void selectDevice(int row);
    Q_INVOKABLE void fastbootFlash(const QString &partition, const QUrl &imageUrl);
    Q_INVOKABLE void fastbootErase(const QString &partition);
    Q_INVOKABLE void fastbootReboot(const QString &target);
    Q_INVOKABLE QVariantMap inspectRawProgram(const QUrl &rawProgramUrl);
    Q_INVOKABLE void edlFlash(const QUrl &rawProgramUrl, const QUrl &programmerUrl);
    Q_INVOKABLE void clearLog();

    // [ANDROID-FLASHER] 新增接口：语言、机型、补丁下载
    Q_INVOKABLE void toggleLanguage();
    Q_INVOKABLE void setLanguage(bool english);
    Q_INVOKABLE void selectProfile(int index);
    Q_INVOKABLE void downloadPatch(const QUrl &patchUrl, const QUrl &directoryUrl);
    Q_INVOKABLE QString suggestedPatchName(const QUrl &patchUrl) const;

signals:
    void devicesChanged();
    void selectedDeviceChanged();
    void isBusyChanged();
    void isQueryingChanged();
    void progressChanged();
    void statusTextChanged();
    void logTextChanged();
    void languageChanged();
    void profileChanged();
    void modelCheckChanged();
    void patchChanged();

    // 语言切换同步给工作线程（操作结果文案也跟随切换）
    void requestLanguage(bool english);

    // 投递给工作线程的请求（Qt::QueuedConnection）
    void requestFastbootFlash(QString devicePath, QString partition, QString filePath);
    void requestFastbootErase(QString devicePath, QString partition);
    void requestFastbootReboot(QString devicePath, QString target);
    void requestFastbootQuery(QString devicePath);
    void requestEdlFlash(QString portName, QString rawProgramPath, QString programmerPath);

private slots:
    void onScanTimeout();
    void onProgressChanged(int percent);
    void onLogMessage(const QString &message);
    void onOperationFinished(bool success, const QString &message);
    void onDeviceVarsReady(const QString &devicePath, const QString &product, const QString &serial);
    void onPatchFinished();

private:
    void setStatus(const QString &text, bool error = false);
    void setStatus(const QString &chinese, const QString &english, bool error = false);
    void setBusy(bool busy);
    void setQuerying(bool querying);
    void setProgress(int progress);
    void appendLog(const QString &line);
    void clearSelection();
    void updateModelCheck();
    QString loc(const QString &chinese, const QString &english) const;
    const DeviceInfo *selectedInfo() const;
    QString resolveProgrammer(const QString &rawProgramPath) const;
    static QString localPath(const QUrl &url);

    AndroidDeviceModel *m_model = nullptr;
    AndroidWorker *m_worker = nullptr;
    QThread *m_thread = nullptr;
    QTimer *m_scanTimer = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_patchReply = nullptr;

    QString m_selectedPath;
    QSet<QString> m_varsRequested;
    QStringList m_log;
    QString m_statusText;
    QString m_profileName;
    QString m_profileCodename;
    QString m_modelCheckText;
    QString m_patchStatus;
    bool m_scannedOnce = false;
    bool m_statusError = false;
    bool m_busy = false;
    bool m_querying = false;
    bool m_english = false;
    bool m_modelMismatch = false;
    bool m_patchBusy = false;
    int m_progress = -1;
    int m_profileIndex = 0;
    int m_patchProgress = 0;
};

} // namespace AndroidFlasher
