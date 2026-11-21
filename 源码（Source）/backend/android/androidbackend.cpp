#include "androidbackend.h"

// [ANDROID-FLASHER] 新增文件实现：安卓刷机后端逻辑
// 本次扩展：中英切换、机型校验、补丁自动下载（尾部新增段落均带 [ANDROID-FLASHER] 标记）

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "androiddevicescanner.h"

namespace AndroidFlasher {

namespace {

QString humanSize(qint64 bytes)
{
    if(bytes >= 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 GB").arg(QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2));
    }

    if(bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024), 'f', 1));
    }

    return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 0));
}

} // namespace

AndroidBackend::AndroidBackend(QObject *parent):
    QObject(parent),
    m_model(new AndroidDeviceModel(this)),
    m_worker(new AndroidWorker),
    m_thread(new QThread(this)),
    m_scanTimer(new QTimer(this)),
    m_network(new QNetworkAccessManager(this)),
    m_statusText(QString()),
    m_patchStatus(QString())
{
    const QVector<ModelProfile> profiles = AndroidFlasher::modelProfiles();

    if(!profiles.isEmpty()) {
        m_profileName = profiles.first().label;
        m_profileCodename = profiles.first().codename;
    }

    // [ANDROID-FLASHER] 补丁：exe 同目录存在 english.flag 时，启动即英文界面。
    // 由「补丁（Patch）\切换语言（Switch-Language）.bat」创建/删除该文件来切换，重启生效。
    m_english = QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/english.flag"));

    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(this, &AndroidBackend::requestFastbootFlash, m_worker, &AndroidWorker::fastbootFlash);
    connect(this, &AndroidBackend::requestFastbootErase, m_worker, &AndroidWorker::fastbootErase);
    connect(this, &AndroidBackend::requestFastbootReboot, m_worker, &AndroidWorker::fastbootReboot);
    connect(this, &AndroidBackend::requestFastbootQuery, m_worker, &AndroidWorker::fastbootQuery);
    connect(this, &AndroidBackend::requestEdlFlash, m_worker, &AndroidWorker::edlFlash);
    connect(this, &AndroidBackend::requestLanguage, m_worker, &AndroidWorker::setEnglish);

    connect(m_worker, &AndroidWorker::progressChanged, this, &AndroidBackend::onProgressChanged);
    connect(m_worker, &AndroidWorker::logMessage, this, &AndroidBackend::onLogMessage);
    connect(m_worker, &AndroidWorker::operationFinished, this, &AndroidBackend::onOperationFinished);
    connect(m_worker, &AndroidWorker::deviceVarsReady, this, &AndroidBackend::onDeviceVarsReady);

    m_thread->start();

    // [ANDROID-FLASHER] 补丁：把启动时读到的语言状态同步给工作线程
    emit requestLanguage(m_english);

    m_scanTimer->setInterval(1200);
    connect(m_scanTimer, &QTimer::timeout, this, &AndroidBackend::onScanTimeout);
    m_scanTimer->start();

    setStatus(QStringLiteral("正在扫描 USB 设备…"),
              QStringLiteral("Scanning USB devices…"));

    QTimer::singleShot(0, this, &AndroidBackend::refreshDevices);
}

AndroidBackend::~AndroidBackend()
{
    m_scanTimer->stop();

    if(m_patchReply) {
        m_patchReply->abort();
    }

    // 退出时如果还在刷机，先置位取消标记，避免线程无法结束
    QMetaObject::invokeMethod(m_worker, "cancel", Qt::DirectConnection);

    m_thread->quit();

    if(!m_thread->wait(3000)) {
        m_thread->terminate();
        m_thread->wait(1000);
    }
}

QAbstractListModel *AndroidBackend::deviceModel() const
{
    return m_model;
}

int AndroidBackend::deviceCount() const
{
    return m_model->rowCount();
}

bool AndroidBackend::isBusy() const
{
    return m_busy;
}

bool AndroidBackend::isQuerying() const
{
    return m_querying;
}

int AndroidBackend::progress() const
{
    return m_progress;
}

QString AndroidBackend::statusText() const
{
    return m_statusText;
}

bool AndroidBackend::statusIsError() const
{
    return m_statusError;
}

QString AndroidBackend::logText() const
{
    return m_log.join(QLatin1Char('\n'));
}

QVariantMap AndroidBackend::selectedDevice() const
{
    QVariantMap map;
    const DeviceInfo *device = selectedInfo();

    if(!device) {
        return map;
    }

    map.insert(QStringLiteral("path"), device->path);
    map.insert(QStringLiteral("port"), device->port);
    map.insert(QStringLiteral("product"), device->product);
    map.insert(QStringLiteral("manufacturer"), device->manufacturer);
    map.insert(QStringLiteral("serial"), device->serial);
    map.insert(QStringLiteral("vidPid"), device->vidPid());
    map.insert(QStringLiteral("detail"), device->detail);
    map.insert(QStringLiteral("mode"), static_cast<int>(device->mode));
    map.insert(QStringLiteral("modeName"), m_english ? modeNameEn(device->mode) : modeName(device->mode));
    map.insert(QStringLiteral("modeKey"), modeKey(device->mode));
    map.insert(QStringLiteral("canFastboot"), device->canFastboot());
    map.insert(QStringLiteral("canEdl"), device->canEdl());
    map.insert(QStringLiteral("isIos"), device->isIos());
    map.insert(QStringLiteral("mtp"), device->mtp);
    map.insert(QStringLiteral("hasWinUsb"), !device->path.isEmpty());

    return map;
}

// [ANDROID-FLASHER] 语言与机型相关属性
bool AndroidBackend::english() const
{
    return m_english;
}

QVariantList AndroidBackend::modelProfiles() const
{
    QVariantList list;

    for(const auto &profile : AndroidFlasher::modelProfiles()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("label"), profile.label);
        entry.insert(QStringLiteral("codename"), profile.codename);
        list.append(entry);
    }

    return list;
}

int AndroidBackend::profileIndex() const
{
    return m_profileIndex;
}

QString AndroidBackend::profileName() const
{
    return m_profileName;
}

QString AndroidBackend::profileCodename() const
{
    return m_profileCodename;
}

QString AndroidBackend::modelCheckText() const
{
    return m_modelCheckText;
}

bool AndroidBackend::modelMismatch() const
{
    return m_modelMismatch;
}

bool AndroidBackend::patchBusy() const
{
    return m_patchBusy;
}

int AndroidBackend::patchProgress() const
{
    return m_patchProgress;
}

QString AndroidBackend::patchStatus() const
{
    return m_patchStatus;
}

QString AndroidBackend::loc(const QString &chinese, const QString &english) const
{
    return m_english ? english : chinese;
}

void AndroidBackend::toggleLanguage()
{
    setLanguage(!m_english);
}

void AndroidBackend::setLanguage(bool english)
{
    if(m_english == english) {
        return;
    }

    m_english = english;
    emit languageChanged();

    // 工作线程里的操作结果文案也要跟着切
    emit requestLanguage(english);

    // 状态栏文案是按语言二选一输出的，切换后按当前设备数重算一次
    if(!m_busy) {
        const int count = deviceCount();

        if(count <= 0) {
            setStatus(QStringLiteral("等待安卓设备接入（USB 调试 / Fastboot / 9008）"),
                      QStringLiteral("Waiting for a device (ADB / Fastboot / 9008)"));
        } else {
            setStatus(QStringLiteral("检测到 %1 个安卓设备").arg(count),
                      QStringLiteral("%1 device(s) detected").arg(count));
        }
    }

    updateModelCheck();

    if(!m_patchStatus.isEmpty()) {
        emit patchChanged();
    }

    emit selectedDeviceChanged();
}

void AndroidBackend::selectProfile(int index)
{
    const QVector<ModelProfile> profiles = AndroidFlasher::modelProfiles();

    if(index < 0 || index >= profiles.size()) {
        return;
    }

    if(m_profileIndex == index) {
        return;
    }

    m_profileIndex = index;
    m_profileName = profiles.at(index).label;
    m_profileCodename = profiles.at(index).codename;

    emit profileChanged();
    updateModelCheck();

    if(m_profileCodename.isEmpty()) {
        appendLog(loc(QStringLiteral("机型：%1（不做代号校验）").arg(m_profileName),
                      QStringLiteral("Model: %1 (codename check off)").arg(m_profileName)));
    } else {
        appendLog(loc(QStringLiteral("机型：%1（代号 %2）").arg(m_profileName, m_profileCodename),
                      QStringLiteral("Model: %1 (codename %2)").arg(m_profileName, m_profileCodename)));
    }
}

void AndroidBackend::updateModelCheck()
{
    QString text;
    bool mismatch = false;

    const DeviceInfo *device = selectedInfo();

    if(m_profileCodename.isEmpty()) {
        text = loc(QStringLiteral("未启用机型校验"), QStringLiteral("model check disabled"));
    } else if(!device) {
        text = loc(QStringLiteral("等待选择设备"), QStringLiteral("waiting for a device"));
    } else if(device->product.isEmpty()) {
        text = loc(QStringLiteral("等待读取设备代号…"), QStringLiteral("reading device codename…"));
    } else {
        const QString product = device->product.toLower();

        if(product.contains(m_profileCodename.toLower())) {
            text = loc(QStringLiteral("机型匹配：%1").arg(device->product),
                       QStringLiteral("model matched: %1").arg(device->product));
        } else {
            mismatch = true;
            text = loc(QStringLiteral("机型不匹配：设备代号 %1 ≠ 所选 %2")
                       .arg(device->product, m_profileCodename),
                       QStringLiteral("model mismatch: device %1 ≠ selected %2")
                       .arg(device->product, m_profileCodename));
        }
    }

    if(m_modelCheckText == text && m_modelMismatch == mismatch) {
        return;
    }

    m_modelCheckText = text;
    m_modelMismatch = mismatch;
    emit modelCheckChanged();
}

void AndroidBackend::onScanTimeout()
{
    refreshDevices();
}

void AndroidBackend::refreshDevices()
{
    const QVector<DeviceInfo> devices = AndroidDeviceScanner::scan();
    const QVector<DeviceInfo> &current = m_model->devices();

    bool changed = devices.size() != current.size();

    if(!changed) {
        for(int i = 0; i < devices.size(); ++i) {
            const DeviceInfo &a = devices.at(i);
            const DeviceInfo &b = current.at(i);

            if(a.key() != b.key() || a.mode != b.mode || a.product != b.product ||
               a.port != b.port || a.mtp != b.mtp) {
                changed = true;
                break;
            }
        }
    }

    if(!changed && m_scannedOnce) {
        return;
    }

    m_scannedOnce = true;

    QStringList presentPaths;

    for(const auto &device : devices) {
        presentPaths.append(device.path);
    }

    QStringList stalePaths;

    for(const QString &path : m_varsRequested) {
        if(!presentPaths.contains(path)) {
            stalePaths.append(path);
        }
    }

    for(const QString &path : stalePaths) {
        m_varsRequested.remove(path);
    }

    m_model->setDevices(devices);
    emit devicesChanged();

    const int row = m_selectedPath.isEmpty() ? -1 : m_model->indexOfPath(m_selectedPath);

    if(row >= 0) {
        emit selectedDeviceChanged();
        updateModelCheck();
    } else if(!devices.isEmpty()) {
        selectDevice(0);
    } else {
        clearSelection();
        updateModelCheck();
    }

    if(m_busy) {
        return;
    }

    if(devices.isEmpty()) {
        setStatus(QStringLiteral("等待安卓设备接入（USB 调试 / Fastboot / 9008）"),
                  QStringLiteral("Waiting for a device (ADB / Fastboot / 9008)"));
    } else {
        setStatus(QStringLiteral("检测到 %1 个安卓设备").arg(devices.size()),
                  QStringLiteral("%1 device(s) detected").arg(devices.size()));
    }
}

void AndroidBackend::selectDevice(int row)
{
    const DeviceInfo *device = m_model->deviceAt(row);

    if(!device) {
        return;
    }

    m_selectedPath = device->key();
    emit selectedDeviceChanged();
    updateModelCheck();

    if(device->mode == DeviceMode::Fastboot && !device->path.isEmpty() &&
       !m_varsRequested.contains(device->path)) {
        m_varsRequested.insert(device->path);
        setQuerying(true);
        emit requestFastbootQuery(device->path);
    }

    if(!m_busy) {
        setStatus(QStringLiteral("已选择：%1（%2）").arg(device->product, modeName(device->mode)),
                  QStringLiteral("Selected: %1 (%2)").arg(device->product, modeNameEn(device->mode)));
    }
}

void AndroidBackend::fastbootFlash(const QString &partition, const QUrl &imageUrl)
{
    if(m_busy) {
        setStatus(QStringLiteral("正在执行其它操作，请稍候"),
                  QStringLiteral("Another operation is running, please wait"), true);
        return;
    }

    const DeviceInfo *device = selectedInfo();

    if(!device || device->mode != DeviceMode::Fastboot) {
        setStatus(QStringLiteral("请先在左侧选择一台 Fastboot 模式的设备"),
                  QStringLiteral("Select a device in Fastboot mode first"), true);
        return;
    }

    if(device->path.isEmpty()) {
        setStatus(QStringLiteral("该设备没有绑定 WinUSB 接口，无法执行 Fastboot 操作。"
                                 "请安装 Google USB Driver 或使用 Zadig 安装 WinUSB"),
                  QStringLiteral("This device has no WinUSB interface. "
                                 "Install the Google USB Driver or use Zadig to bind WinUSB"), true);
        return;
    }

    if(m_modelMismatch) {
        setStatus(QStringLiteral("机型校验未通过（%1），已拒绝刷入").arg(m_modelCheckText),
                  QStringLiteral("Model check failed (%1), flashing refused").arg(m_modelCheckText), true);
        return;
    }

    const QString part = partition.trimmed();

    if(part.isEmpty()) {
        setStatus(QStringLiteral("请填写要刷入的分区名（例如 boot / system / recovery）"),
                  QStringLiteral("Enter a partition name (e.g. boot / system / recovery)"), true);
        return;
    }

    const QString imagePath = localPath(imageUrl);

    if(!imagePath.isEmpty() && !QFileInfo::exists(imagePath)) {
        setStatus(QStringLiteral("镜像文件不存在：%1").arg(imagePath),
                  QStringLiteral("Image file not found: %1").arg(imagePath), true);
        return;
    }

    if(imagePath.isEmpty()) {
        setStatus(QStringLiteral("请先选择要刷入的镜像文件"),
                  QStringLiteral("Choose an image file first"), true);
        return;
    }

    appendLog(loc(QStringLiteral("Fastboot：准备刷入 %1 → %2")
                  .arg(QFileInfo(imagePath).fileName(), part),
                  QStringLiteral("Fastboot: flash %1 -> %2")
                  .arg(QFileInfo(imagePath).fileName(), part)));

    setBusy(true);
    setProgress(0);
    setStatus(QStringLiteral("正在刷入 %1 到分区 %2…").arg(QFileInfo(imagePath).fileName(), part),
              QStringLiteral("Flashing %1 into %2…").arg(QFileInfo(imagePath).fileName(), part));

    emit requestFastbootFlash(device->path, part, imagePath);
}

void AndroidBackend::fastbootErase(const QString &partition)
{
    if(m_busy) {
        setStatus(QStringLiteral("正在执行其它操作，请稍候"),
                  QStringLiteral("Another operation is running, please wait"), true);
        return;
    }

    const DeviceInfo *device = selectedInfo();

    if(!device || device->mode != DeviceMode::Fastboot) {
        setStatus(QStringLiteral("请先在左侧选择一台 Fastboot 模式的设备"),
                  QStringLiteral("Select a device in Fastboot mode first"), true);
        return;
    }

    if(device->path.isEmpty()) {
        setStatus(QStringLiteral("该设备没有绑定 WinUSB 接口，无法执行 Fastboot 操作"),
                  QStringLiteral("This device has no WinUSB interface"), true);
        return;
    }

    if(m_modelMismatch) {
        setStatus(QStringLiteral("机型校验未通过（%1），已拒绝擦除").arg(m_modelCheckText),
                  QStringLiteral("Model check failed (%1), erase refused").arg(m_modelCheckText), true);
        return;
    }

    const QString part = partition.trimmed();

    if(part.isEmpty()) {
        setStatus(QStringLiteral("请填写要擦除的分区名"),
                  QStringLiteral("Enter a partition name to erase"), true);
        return;
    }

    appendLog(loc(QStringLiteral("Fastboot：擦除分区 %1").arg(part),
                  QStringLiteral("Fastboot: erase %1").arg(part)));

    setBusy(true);
    setProgress(0);
    setStatus(QStringLiteral("正在擦除分区 %1…").arg(part),
              QStringLiteral("Erasing %1…").arg(part));

    emit requestFastbootErase(device->path, part);
}

void AndroidBackend::fastbootReboot(const QString &target)
{
    if(m_busy) {
        setStatus(QStringLiteral("正在执行其它操作，请稍候"),
                  QStringLiteral("Another operation is running, please wait"), true);
        return;
    }

    const DeviceInfo *device = selectedInfo();

    if(!device || device->mode != DeviceMode::Fastboot) {
        setStatus(QStringLiteral("请先在左侧选择一台 Fastboot 模式的设备"),
                  QStringLiteral("Select a device in Fastboot mode first"), true);
        return;
    }

    if(device->path.isEmpty()) {
        setStatus(QStringLiteral("该设备没有绑定 WinUSB 接口，无法执行 Fastboot 操作"),
                  QStringLiteral("This device has no WinUSB interface"), true);
        return;
    }

    appendLog(loc(QStringLiteral("Fastboot：重启（%1）").arg(target),
                  QStringLiteral("Fastboot: reboot (%1)").arg(target)));

    setBusy(true);
    setProgress(0);
    setStatus(QStringLiteral("正在发送重启命令（%1）…").arg(target),
              QStringLiteral("Sending reboot (%1)…").arg(target));

    emit requestFastbootReboot(device->path, target);
}

QVariantMap AndroidBackend::inspectRawProgram(const QUrl &rawProgramUrl)
{
    QVariantMap result;
    result.insert(QStringLiteral("valid"), false);

    const QString path = localPath(rawProgramUrl);

    if(path.isEmpty()) {
        result.insert(QStringLiteral("message"),
                      loc(QStringLiteral("尚未选择 rawprogram 文件"),
                          QStringLiteral("No rawprogram file selected")));
        return result;
    }

    QVector<RawProgramEntry> entries;
    QString error;

    if(!RawProgramParser::parse(path, &entries, &error)) {
        result.insert(QStringLiteral("message"), error);
        return result;
    }

    const QDir dir(QFileInfo(path).absolutePath());
    qint64 totalBytes = 0;
    int missingFiles = 0;

    for(const auto &entry : entries) {
        const QFileInfo file(dir.filePath(entry.filename));

        if(!file.exists()) {
            ++missingFiles;
            continue;
        }

        totalBytes += entry.sizeInBytes > 0 ? entry.sizeInBytes : file.size();
    }

    result.insert(QStringLiteral("valid"), true);
    result.insert(QStringLiteral("count"), entries.size());
    result.insert(QStringLiteral("bytes"), totalBytes);
    result.insert(QStringLiteral("missing"), missingFiles);

    if(missingFiles > 0) {
        result.insert(QStringLiteral("message"),
                      loc(QStringLiteral("共 %1 个分区，缺少 %2 个镜像文件")
                          .arg(entries.size()).arg(missingFiles),
                          QStringLiteral("%1 partition(s), %2 image file(s) missing")
                          .arg(entries.size()).arg(missingFiles)));
    } else {
        result.insert(QStringLiteral("message"),
                      loc(QStringLiteral("共 %1 个分区，约 %2")
                          .arg(entries.size()).arg(humanSize(totalBytes)),
                          QStringLiteral("%1 partition(s), about %2")
                          .arg(entries.size()).arg(humanSize(totalBytes))));
    }

    return result;
}

void AndroidBackend::edlFlash(const QUrl &rawProgramUrl, const QUrl &programmerUrl)
{
    if(m_busy) {
        setStatus(QStringLiteral("正在执行其它操作，请稍候"),
                  QStringLiteral("Another operation is running, please wait"), true);
        return;
    }

    const DeviceInfo *device = selectedInfo();

    if(!device || device->mode != DeviceMode::Qdl) {
        setStatus(QStringLiteral("请先在左侧选择一台高通 9008 (QDL) 设备"),
                  QStringLiteral("Select a Qualcomm 9008 (QDL) device first"), true);
        return;
    }

    if(device->port.isEmpty()) {
        setStatus(QStringLiteral("没有识别到 9008 串口，请检查高通 HS-USB QDLoader 驱动"),
                  QStringLiteral("No 9008 COM port found, check the Qualcomm HS-USB QDLoader driver"), true);
        return;
    }

    const QString xmlPath = localPath(rawProgramUrl);

    if(xmlPath.isEmpty() || !QFileInfo::exists(xmlPath)) {
        setStatus(QStringLiteral("请选择固件目录中的 rawprogram*.xml"),
                  QStringLiteral("Choose a rawprogram*.xml from the firmware folder"), true);
        return;
    }

    QVector<RawProgramEntry> entries;
    QString error;

    if(!RawProgramParser::parse(xmlPath, &entries, &error)) {
        setStatus(error, error, true);
        return;
    }

    QString programmerPath = localPath(programmerUrl);

    if(programmerPath.isEmpty()) {
        programmerPath = resolveProgrammer(xmlPath);
    }

    if(programmerPath.isEmpty() || !QFileInfo::exists(programmerPath)) {
        setStatus(QStringLiteral("没有找到 programmer 文件（prog_firehose_*.mbn），"
                                 "请手动在固件目录中选择"),
                  QStringLiteral("Programmer file not found (prog_firehose_*.mbn), pick it manually"), true);
        return;
    }

    appendLog(loc(QStringLiteral("9008：使用 %1 烧录 %2 个分区")
                  .arg(QFileInfo(programmerPath).fileName()).arg(entries.size()),
                  QStringLiteral("9008: using %1 to write %2 partition(s)")
                  .arg(QFileInfo(programmerPath).fileName()).arg(entries.size())));

    setBusy(true);
    setProgress(0);
    setStatus(QStringLiteral("开始 9008 烧录，共 %1 个分区…").arg(entries.size()),
              QStringLiteral("Starting 9008 flash, %1 partition(s)…").arg(entries.size()));

    emit requestEdlFlash(device->port, xmlPath, programmerPath);
}

void AndroidBackend::clearLog()
{
    m_log.clear();
    emit logTextChanged();
}

// [ANDROID-FLASHER] 补丁自动下载：只负责把文件下到本地，不自动写入设备
QString AndroidBackend::suggestedPatchName(const QUrl &patchUrl) const
{
    const QString path = patchUrl.path();
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    QString name = slash >= 0 ? path.mid(slash + 1) : path;

    if(name.isEmpty()) {
        name = QStringLiteral("patch.bin");
    }

    return name;
}

void AndroidBackend::downloadPatch(const QUrl &patchUrl, const QUrl &directoryUrl)
{
    if(m_patchBusy) {
        m_patchStatus = loc(QStringLiteral("已有一个补丁正在下载，请稍候"),
                            QStringLiteral("A patch is already downloading"));
        emit patchChanged();
        return;
    }

    if(!patchUrl.isValid() || patchUrl.isEmpty()) {
        m_patchStatus = loc(QStringLiteral("请先填写补丁下载地址"),
                            QStringLiteral("Enter a patch URL first"));
        emit patchChanged();
        return;
    }

    const QString scheme = patchUrl.scheme().toLower();

    if(scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        m_patchStatus = loc(QStringLiteral("只支持 http / https 地址"),
                            QStringLiteral("Only http / https URLs are supported"));
        emit patchChanged();
        return;
    }

    QString dirPath = directoryUrl.toLocalFile();

    if(dirPath.isEmpty()) {
        dirPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }

    if(dirPath.isEmpty()) {
        dirPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    QDir dir(dirPath);

    if(!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        m_patchStatus = loc(QStringLiteral("无法创建保存目录：%1").arg(dirPath),
                            QStringLiteral("Cannot create folder: %1").arg(dirPath));
        emit patchChanged();
        return;
    }

    QString targetName = suggestedPatchName(patchUrl);
    QString targetPath = dir.filePath(targetName);
    int suffix = 1;

    while(QFileInfo::exists(targetPath) && suffix < 100) {
        const QString base = QFileInfo(targetName).completeBaseName();
        const QString ext = QFileInfo(targetName).suffix();
        const QString candidate = ext.isEmpty() ?
                                  QStringLiteral("%1_%2").arg(base).arg(suffix) :
                                  QStringLiteral("%1_%2.%3").arg(base).arg(suffix).arg(ext);
        targetPath = dir.filePath(candidate);
        ++suffix;
    }

    QNetworkRequest request(patchUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_patchBusy = true;
    m_patchProgress = 0;
    m_patchStatus = loc(QStringLiteral("开始下载：%1").arg(targetName),
                        QStringLiteral("Downloading: %1").arg(targetName));
    emit patchChanged();

    appendLog(loc(QStringLiteral("补丁下载：%1 → %2").arg(patchUrl.toString(), targetPath),
                  QStringLiteral("Patch download: %1 -> %2").arg(patchUrl.toString(), targetPath)));

    m_patchReply = m_network->get(request);

    connect(m_patchReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        if(total > 0) {
            m_patchProgress = static_cast<int>((received * 100) / total);
        }

        m_patchStatus = loc(QStringLiteral("下载中 %1 / %2").arg(humanSize(received), humanSize(total)),
                            QStringLiteral("Downloading %1 / %2").arg(humanSize(received), humanSize(total)));
        emit patchChanged();
    });

    connect(m_patchReply, &QNetworkReply::finished, this, [this, targetPath]() {
        QNetworkReply *reply = m_patchReply;
        m_patchReply = nullptr;

        const bool ok = reply && reply->error() == QNetworkReply::NoError;
        const QByteArray payload = ok ? reply->readAll() : QByteArray();

        if(reply) {
            reply->deleteLater();
        }

        m_patchBusy = false;

        if(!ok) {
            m_patchProgress = -1;
            m_patchStatus = loc(QStringLiteral("下载失败：%1").arg(
                                    reply ? reply->errorString() : QStringLiteral("unknown")),
                                QStringLiteral("Download failed: %1").arg(
                                    reply ? reply->errorString() : QStringLiteral("unknown")));
            emit patchChanged();
            appendLog(m_patchStatus);
            return;
        }

        QFile file(targetPath);

        if(!file.open(QIODevice::WriteOnly)) {
            m_patchProgress = -1;
            m_patchStatus = loc(QStringLiteral("无法写入文件：%1").arg(targetPath),
                                QStringLiteral("Cannot write file: %1").arg(targetPath));
        } else {
            file.write(payload);
            file.close();

            m_patchProgress = 100;
            m_patchStatus = loc(QStringLiteral("已保存 %1（%2）")
                                .arg(QFileInfo(targetPath).fileName(), humanSize(payload.size())),
                                QStringLiteral("Saved %1 (%2)")
                                .arg(QFileInfo(targetPath).fileName(), humanSize(payload.size())));
        }

        emit patchChanged();
        appendLog(m_patchStatus);
    });
}

void AndroidBackend::onPatchFinished()
{
    emit patchChanged();
}

void AndroidBackend::onProgressChanged(int percent)
{
    setProgress(percent);
}

void AndroidBackend::onLogMessage(const QString &message)
{
    appendLog(message);
}

void AndroidBackend::onOperationFinished(bool success, const QString &message)
{
    setBusy(false);
    setProgress(success ? 100 : -1);
    setStatus(message, message, !success);
    appendLog((success ? loc(QStringLiteral("[成功] "), QStringLiteral("[OK] "))
                       : loc(QStringLiteral("[失败] "), QStringLiteral("[FAIL] ")) ) + message);
}

void AndroidBackend::onDeviceVarsReady(const QString &devicePath, const QString &product, const QString &serial)
{
    m_model->updateProduct(devicePath, product, serial);
    emit selectedDeviceChanged();
    updateModelCheck();
    setQuerying(false);
}

void AndroidBackend::setStatus(const QString &text, bool error)
{
    // 单语言重载：中英文案相同时使用（例如协议层返回的错误文本）
    setStatus(text, text, error);
}

void AndroidBackend::setStatus(const QString &chinese, const QString &english, bool error)
{
    const QString text = loc(chinese, english);

    if(m_statusText == text && m_statusError == error) {
        return;
    }

    m_statusText = text;
    m_statusError = error;
    emit statusTextChanged();
}

void AndroidBackend::setBusy(bool busy)
{
    if(m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit isBusyChanged();
}

void AndroidBackend::setQuerying(bool querying)
{
    if(m_querying == querying) {
        return;
    }

    m_querying = querying;
    emit isQueryingChanged();
}

void AndroidBackend::setProgress(int progress)
{
    if(m_progress == progress) {
        return;
    }

    m_progress = progress;
    emit progressChanged();
}

void AndroidBackend::appendLog(const QString &line)
{
    if(line.isEmpty()) {
        return;
    }

    m_log.append(line);

    while(m_log.size() > 300) {
        m_log.removeFirst();
    }

    emit logTextChanged();
}

void AndroidBackend::clearSelection()
{
    if(m_selectedPath.isEmpty()) {
        return;
    }

    m_selectedPath.clear();
    emit selectedDeviceChanged();
}

const DeviceInfo *AndroidBackend::selectedInfo() const
{
    return m_model->deviceByPath(m_selectedPath);
}

QString AndroidBackend::resolveProgrammer(const QString &rawProgramPath) const
{
    QFileInfo fallback;

    QDir dir(QFileInfo(rawProgramPath).absolutePath());

    for(int level = 0; level < 2; ++level) {
        const QFileInfoList entries = dir.entryInfoList(QDir::Files);

        for(const QFileInfo &file : entries) {
            const QString name = file.fileName().toLower();
            const QString suffix = file.suffix().toLower();

            if(suffix != QStringLiteral("mbn") && suffix != QStringLiteral("elf") &&
               suffix != QStringLiteral("bin")) {
                continue;
            }

            if(!name.contains(QStringLiteral("firehose"))) {
                continue;
            }

            if(name.startsWith(QStringLiteral("prog_"))) {
                return file.absoluteFilePath();
            }

            if(!fallback.isFile()) {
                fallback = file;
            }
        }

        if(fallback.isFile()) {
            return fallback.absoluteFilePath();
        }

        if(!dir.cdUp()) {
            break;
        }
    }

    return QString();
}

QString AndroidBackend::localPath(const QUrl &url)
{
    const QString path = url.toLocalFile();

    return path.isEmpty() ? url.toString() : path;
}

} // namespace AndroidFlasher
