# 安卓刷机模块（Android Flasher）— 新增代码说明与编译指南

> 适用工程：本目录下的 qFlipper（Qt 6 /qmake，Windows + MSVC）
> 目标：在保留 Flipper 全部原有功能的前提下，新增一个与之并存的「安卓刷机」标签页。
> 所有新增 / 修改处均带有 
>
> `[ANDROID-FLASHER]`
>
>  注释标记，可用一条命令全部定位（见第 6 节）。



***

## 1. 功能范围



| 能力                                       | 实现情况                                                                                 |
| ---------------------------------------- | ------------------------------------------------------------------------------------ |
| 自动扫描 USB 设备                              | 每 1.2 s 轮询 Windows SetupAPI 枚举，设备集合变化时刷新列表                                           |
| 区分 普通安卓 (ADB) / Fastboot / 高通 9008 (QDL) | 依据接口类 `FF/42/01`、`FF/42/03`、VID 0x05C6 + 下载口 PID，另有名称关键字兜底                           |
| 读取设备信息                                   | 型号、制造商、VID:PID、序列号、COM 端口、WinUSB 路径、接口描述                                             |
| Fastboot：刷镜像                             | `download:<size>` → 分块上传 → `flash:<分区>`，支持 1 MiB 分块与进度                               |
| Fastboot：重启 / 擦除                         | `reboot` / `reboot-bootloader` / `reboot-recovery` / `reboot-fastboot`；`erase:<分区>`  |
| 9008：按 rawprogram.xml 烧录                 | Sahara v2 上传 programmer → Firehose `configure` → 逐分区 `program` + 数据流 → `power reset` |
| 界面分离、互不冲突                                | 标题栏两个标签块切换 `currentPanel`，Flipper 5 个 overlay 增加 `visible/enabled` 条件，安卓面板独立文件       |
| 保持原有目录结构                                 | 后端新建 `backend/android/` 子模块（9 组 .h/.cpp，按职责拆分），未合并成单文件                               |



***

## 2. 新增文件清单

### 2.1 后端模块 `backend/android/`



| 文件                            | 职责                                                                                                                    | 关键接口                                                                                                                                           |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `androidtypes.h`              | 纯数据结构与常量：设备模式枚举、`DeviceInfo`、模式名 / 键、高通下载口 PID 表、常见安卓厂商 VID 表                                                         | `DeviceMode`、`DeviceInfo::key()/canFastboot()/canEdl()`、`modeName()`、`modeKey()`、`isQualcommLoaderId()`、`isKnownAndroidVendor()`               |
| `androidusbtransport.h/.cpp`  | WinUSB 原始批量传输封装（PIMPL）：打开设备接口、查询 bulk 端点、重叠 I/O 读写、端点复位                                                               | `open()/close()/isOpen()/write()/read()/resetPipes()`                                                                                          |
| `androiddevicescanner.h/.cpp` | SetupAPI 枚举 `USB` 枚举器下所有设备节点，读取 FriendlyName / DeviceDesc / MFG / CompatibleIDs / BusReportedDeviceDesc / COM 号，并完成分类 | `static scan()`、`static findQualcommPort()`、`static availableSerialPorts()`                                                                    |
| `androiddevicemodel.h/.cpp`   | `QAbstractListModel`，向 QML `ListView` 提供设备行；支持 Fastboot 查询后局部回填型号 / 序列号                                               | `setDevices()`、`updateProduct()`、`deviceAt()`、`deviceByPath()`                                                                                 |
| `fastbootclient.h/.cpp`       | Fastboot 文本协议客户端（走 bulk 传输）：命令发送、`OKAY/FAIL/INFO/DATA` 解析、下载与刷写                                                       | `open/close/getvar/flash/erase/reboot`                                                                                                         |
| `rawprogramparser.h/.cpp`     | `QXmlStreamReader` 解析 `rawprogram*.xml` 的 `<program>` 条目                                                              | `static parse()`、`RawProgramEntry`                                                                                                             |
| `edlclient.h/.cpp`            | 9008 全流程：Sahara v2 握手 / 上传 programmer / 重连 Firehose 串口 /configure/ 逐分区烧录 / 重启                                         | `flash(port, loader, entries, baseDir, progress, log, error)`、`cancel()`                                                                       |
| `androidworker.h/.cpp`        | 工作线程对象：所有阻塞式 USB / 串口操作在此执行，结果以信号回主线程                                                                                 | 槽 `fastbootFlash/fastbootErase/fastbootReboot/fastbootQuery/edlFlash/cancel`；信号 `progressChanged/logMessage/operationFinished/deviceVarsReady` |
| `androidbackend.h/.cpp`       | QML 单例 `AndroidBackend`：扫描定时器、设备模型、选中项、忙碌 / 进度 / 状态 / 日志、请求投递到工作线程、programmer 自动查找                                    | 见第 4 节属性与方法表                                                                                                                                   |

### 2.2 前端



| 文件                                          | 说明                                                                        |
| ------------------------------------------- | ------------------------------------------------------------------------- |
| `application/components/AndroidOverlay.qml` | 安卓面板全部界面：标题、设备列表（268×300 卡片）、设备信息卡、操作卡（Fastboot / 9008 / 提示三态互斥）、底部进度与状态条 |



***

## 3. 修改文件清单（精确位置）

行号为当前版本实测值，若后续再编辑会漂移；建议按 `[ANDROID-FLASHER]` 标记定位。



| 文件                                      | 行号                                              | 标记                         | 改动内容                                                                                                                                 |
| --------------------------------------- | ----------------------------------------------- | -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `backend/backend.pro`                   | 12–19                                           | （无注释，位于 `SOURCES +=` 首部）   | 新增 8 个 `android/*.cpp`                                                                                                               |
| `backend/backend.pro`                   | 123–131                                         | （无注释，位于 `HEADERS +=` 首部）   | 新增 9 个 `android/*.h`                                                                                                                 |
| `application/application.h`             | 13–14                                           | `[ANDROID-FLASHER] 新增`     | `#include "android/androidbackend.h"`                                                                                                |
| `application/application.h`             | 78–79                                           | `[ANDROID-FLASHER] 新增`     | 成员 `AndroidFlasher::AndroidBackend m_androidBackend;`                                                                                |
| `application/application.cpp`           | 200–201                                         | `[ANDROID-FLASHER] 新增`     | `qmlRegisterSingletonInstance("QFlipper",1,0,"AndroidBackend",&m_androidBackend);`（紧随 `Backend` 之后）                                  |
| `application/qml.qrc`                   | 150–151                                         | `[ANDROID-FLASHER] 新增`     | 注册 `components/AndroidOverlay.qml`                                                                                                   |
| `application/components/MainWindow.qml` | 23–24                                           | `[ANDROID-FLASHER] 新增`     | `property int currentPanel: 0`                                                                                                       |
| 同上                                      | 150–212                                         | `[ANDROID-FLASHER] 新增`     | 标题栏左侧两个标签块 `Row id: panelTabs`（FLIPPER 116×26 / 安卓刷机 126×26，y=6，高亮切换 `currentPanel`）                                                 |
| 同上                                      | 245–247                                         | `[ANDROID-FLASHER] 修改`     | `DeviceWidget`：`visible: currentPanel===0 && opacity>0`、`enabled: currentPanel===0`                                                  |
| 同上                                      | 262–263 / 271–272 / 281–282 / 291–292 / 299–300 | `[ANDROID-FLASHER] 修改`     | `NoDeviceOverlay` / `HomeOverlay` / `UpdateOverlay` / `FinishOverlay` / `StreamOverlay` 各增加 `visible: currentPanel===0 && opacity>0` |
| 同上                                      | 303–308                                         | `[ANDROID-FLASHER] 新增`     | `AndroidOverlay { anchors.fill: parent; visible: currentPanel===1 }`（`mainContent` 最后一个子项）                                           |
| 同上                                      | 370–371                                         | `[ANDROID-FLASHER] 修改`     | 底部 `StatusBar` 增加 `androidMode: currentPanel===1`                                                                                    |
| `application/components/StatusBar.qml`  | 12–18                                           | `[ANDROID-FLASHER] 新增`     | `property bool androidMode`；`errorOccured` 改为按面板分支                                                                                   |
| 同上                                      | 20–35                                           | `[ANDROID-FLASHER] 修改（搬运）` | 原状态文本判定链整体搬入 `readonly property string flipperText`                                                                                  |
| 同上                                      | 54–55                                           | `[ANDROID-FLASHER] 修改`     | `message.text: androidMode ? AndroidBackend.statusText : flipperText`                                                                |

一次性定位全部改动（PowerShell，在工程根执行）：



```
Get-ChildItem -Recurse -Include *.h,*.cpp,*.qml,*.pro,*.qrc |
  Select-String -Pattern 'ANDROID-FLASHER' |
  ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }
```



***

## 4. QML 接口（`AndroidBackend` 单例）

属性：



| 属性               | 类型                    | 说明                                                                                                           |
| ---------------- | --------------------- | ------------------------------------------------------------------------------------------------------------ |
| `deviceModel`    | `QAbstractListModel*` | 设备列表模型，role：`port/product/modeName/modeKey/serial/vidPid/path/detail/canFastboot/canEdl/mtp`                 |
| `deviceCount`    | int                   | 设备数量                                                                                                         |
| `isBusy`         | bool                  | 是否正在刷机 / 烧录                                                                                                  |
| `isQuerying`     | bool                  | 是否正在向 Fastboot 查询 getvar                                                                                     |
| `progress`       | int                   | 0–100，`-1` 表示未知 / 空闲                                                                                         |
| `statusText`     | string                | 状态栏文案                                                                                                        |
| `statusIsError`  | bool                  | 状态栏错误配色开关                                                                                                    |
| `selectedDevice` | QVariantMap           | `path/port/product/manufacturer/serial/vidPid/detail/mode/modeName/modeKey/canFastboot/canEdl/mtp/hasWinUsb` |
| `logText`        | string                | 每行一条，最多 300 行                                                                                                |

方法：`refreshDevices()`、`selectDevice(int)`、`fastbootFlash(分区, 镜像Url)`、`fastbootErase(分区)`、`fastbootReboot(target)`、`inspectRawProgram(xmlUrl)`、`edlFlash(xmlUrl, programmerUrl)`、`clearLog()`。



***

## 5. 实现要点（为什么这么写）

### 5.1 设备识别规则



| 判定       | 条件                                                                      | 结果                                                        |
| -------- | ----------------------------------------------------------------------- | --------------------------------------------------------- |
| 高通下载口    | VID `0x05C6` 且 PID ∈ `9008/900E/901D/9024/9091`，或名称含 `qdloader`         | QDL，COM 号来自 FriendlyName 的 `(COMx)`，失败再读注册表 `PortName`    |
| ADB      | CompatibleIDs 含 `Class_FF&SubClass_42&Prot_01`                          | 普通安卓                                                      |
| Fastboot | CompatibleIDs 含 `Class_FF&SubClass_42&Prot_03`                          | Fastboot                                                  |
| MTP      | `Class_06&SubClass_01` 且厂商 VID 已知                                       | 普通安卓（标记 `mtp`），同一 VID/PID 若同时存在 ADB/Fastboot 接口则丢弃 MTP 条目 |
| 兜底       | 描述含 `fastboot`/`bootloader` → Fastboot；含 `adb` 或 `android` + 已知厂商 → ADB | —                                                         |

型号优先取 `DEVPKEY_Device_BusReportedDeviceDesc`，其次 FriendlyName（去掉 `(COMx)`），Fastboot 设备选中后自动 `getvar:product` / `getvar:serialno` 回填。

### 5.2 9008 走虚拟串口

QFIL/QPST 同样是打开 `Qualcomm HS-USB QDLoader 9008 (COMx)` 虚拟串口（115200 8N1），因此**不需要**用 Zadig 把 9008 口换成 WinUSB；Fastboot 才是走 WinUSB bulk 的（与 AOSP fastboot 一致）。

Sahara v2 命令字与包结构（4×u32 头：`cmd,len,seq,status`，`len` 为含头总长）：



| 命令                    | 值           | 说明                                                                           |
| --------------------- | ----------- | ---------------------------------------------------------------------------- |
| HELLO\_REQ            | 0x01        | 设备→主机，48 字节（payload 40：version/version\_supported/cmd\_packet\_length/mode…） |
| HELLO\_RSP            | 0x02        | 主机→设备，12×u32：`2,0x30,2,1,0,0,1,2,3,4,5,6`（mode=0 → 准备接收镜像）                   |
| READ\_DATA            | 0x03        | 设备要求某段数据：payload = image\_id, offset, length                                 |
| 64BIT\_READ           | 0x12        | 同上的 64 位版本（offset/length 各 8 字节）                                             |
| END\_TRANSFER         | 0x04        | 负载 = image\_id, status（status==0 表示接收成功）                                     |
| DONE\_REQ / DONE\_RSP | 0x05 / 0x06 | 主机确认完成 / 设备回完成                                                               |

上传完 programmer 后：设备回 END\_TRANSFER → 主机发 DONE\_REQ → 设备回 DONE\_RSP → 关闭串口、重新枚举（最多 40×500 ms），拿到新的下载口后进入 Firehose。

Firehose 报文（每条命令一个 XML，响应 `<response value="ACK" …/>` 为成功，`<log value="…"/>` 里是设备侧日志）：



```
<configure MemoryName="emmc" MaxPayloadSizeToTargetInBytes="1048576" ZLPAwareHost="1" SkipWrite="0" />
<data>
  <program SECTOR_SIZE_IN_BYTES="512" num_partition_sectors="131072"
           physical_partition_number="0" start_sector="2048" />
</data>
```

收到 program 的 ACK 后连续写数据（末尾不足一扇区补 0），写完再等一次最终 ACK；全部分区完成后 `<power value="reset" />`。`configure` 先试 `emmc`，失败自动再试 `ufs`。

### 5.3 通道与线程

请求链路：`AndroidOverlay.qml` → `AndroidBackend`（主线程，只做校验与状态）→ `AndroidWorker`（独立 `QThread`，阻塞操作）→ `FastbootClient` / `EdlClient` → `AndroidUsbTransport` / `QSerialPort`。结果通过 `progressChanged / logMessage / operationFinished` 回到主线程，QML 不会被卡住。

### 5.4 界面互斥

`MainWindow` 只新增一个 `currentPanel` 状态：0 显示原有 Flipper 五个 overlay，1 显示 `AndroidOverlay`。两套面板是兄弟节点 + `visible` 互斥，不共享布局，也不改动 Flipper 的任何业务逻辑。底部 `StatusBar` 通过 `androidMode` 决定显示哪套文案与配色。



***

## 6. Windows 编译步骤

### 6.1 前置



1. **Qt 6**：Qt Online Installer 勾选 `Qt 6.x → MSVC 2019 64-bit`（官方 `build_windows.bat` 默认 `Qt 6.4.2` + `msvc2019_64`，其它 6.x 版本同理可用）。

2. **编译器**：Visual Studio 2019/2022 的「使用 C++ 的桌面开发」；或直接装 Qt Creator 并用它自带的 MSVC 套件。

3. 依赖项已自带（`3rdparty/`、`plugins/`），无需额外拉取子模块。

> 路径提醒：当前工程位于 
>
> `...\逆向工具\qFlipper-dev`
>
> ，路径含中文。qmake/MSVC 一般能处理，但若 
>
> `jom`
>
> /
>
> `nmake`
>
>  报路径或编码错误，把整个 
>
> `qFlipper-dev`
>
>  复制到纯英文路径（例如 
>
> `C:\qFlipper-dev`
>
> ）再构建，最省事。

### 6.2 路线 A：Qt Creator（推荐）



1. 打开 `qFlipper-dev/qFlipper.pro`（subdirs 工程，会一起构建 `3rdparty / plugins / dfu / backend / application / cli`）。

2. 选择套件「Desktop Qt 6.x MSVC2019 64bit」，构建模式选 Release。

3. 直接「构建」，产物在影子构建目录根部：`qFlipper.exe`（GUI）与 `qFlipper-cli.exe`。

4. 若改了 QML 想立即看效果，把构建配置里的 `CONFIG+=qtquickcompiler` 去掉可加快迭代。

### 6.3 路线 B：命令行

在「x64 Native Tools Command Prompt for VS 2019/2022」里执行（把 Qt 路径换成自己的）：



```
set QT_DIR=C:\Qt\6.4.2\msvc2019_64
set PATH=%QT_DIR%\bin;%PATH%
cd /d C:\qFlipper-dev
mkdir build & cd build
qmake ..\qFlipper.pro -spec win32-msvc "CONFIG+=qtquickcompiler"
nmake qmake_all
nmake
```

`jom` 可用时可把 `nmake` 换成 `jom`（多核更快）。

### 6.4 单独运行 / 分发



* 直接运行 `build\qFlipper.exe` 时若提示缺 Qt DLL，把 `%QT_DIR%\bin` 加入 `PATH`，或在产物目录执行：



```
windeployqt --release --qmldir ..\application qFlipper.exe
```



* 工程自带的 `build_windows.bat` 会自动完成「配置 → 构建 → windeployqt → 打包 / NSIS 安装包」，但脚本里写死了 Qt 版本、NSIS 路径和 STM32 驱动目录，按需改顶部变量即可。

### 6.5 可能踩到的编译问题



| 现象                                                       | 处理                                                                                                                  |
| -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| 找不到 `devpkey.h` / `DEVPKEY_Device_BusReportedDeviceDesc` | 安装 Windows 10/11 SDK（VS 安装器勾选「Windows SDK」）                                                                         |
| `winusb.h` 相关类型未定义                                       | 本模块已按 `windows.h → winusb.h → usb.h` 顺序包含，若仍报错，确认未在其它头里先包含 `usb.h`                                                  |
| 链接错误 `WinUsb_*` / `SetupDi*` 未解析                         | `application.pro` 通过 `qflipper_common.pri` 已在 win32 下链接 `SetupApi`、`Winusb`、`User32`，确认没有删掉这行                       |
| QML 报 `AndroidBackend is not defined`                    | 检查 `application.cpp` 的 `qmlRegisterSingletonInstance` 是否在 `initQmlTypes()` 内、以及 `qml.qrc` 是否包含 `AndroidOverlay.qml` |
| 面板切换后 Flipper 界面仍可见                                      | 检查 5 个 overlay 的 `visible` 绑定是否被后续编辑覆盖                                                                              |



***

## 7. 使用步骤

### 7.1 普通安卓（ADB）



1. 手机开启「开发者选项 → USB 调试」，用数据线连接。

2. 打开 qFlipper，点标题栏「安卓刷机」，列表出现设备，模式显示「普通安卓 (ADB)」。

3. 该模式只做识别与信息展示，刷机需进入 Fastboot 或 9008。

### 7.2 Fastboot



1. 手机进入 Bootloader：`adb reboot bootloader`，或关机后按「音量下 + 电源」。

2. Windows 需要为 Fastboot 接口安装 WinUSB 驱动（Google USB Driver，或 Zadig 选 WinUSB）。列表里若提示「非 WinUSB 驱动」，刷机按钮会给出提示。

3. 分区名填 `boot` / `recovery` / `system` 等，点「选择镜像」选 `.img`，再点「刷入镜像」。

4. 重启：`系统` = `reboot`、`BOOTLOADER` = `reboot-bootloader`、`RECOVERY` = `reboot-recovery`。

5. 擦除：填分区名后点「擦除分区」（会清空该分区数据）。

### 7.3 高通 9008



1. 关机，按住「音量上 + 音量下」（部分机型仅音量上）插 USB，设备管理器出现 `Qualcomm HS-USB QDLoader 9008 (COMx)`。

2. 面板点「选择 XML」选固件目录里的 `rawprogram0.xml`（会有分区数与总体积预统计，缺文件会提示）。

3. programmer 会自动在 XML 同目录及上一级找 `prog_firehose_*.mbn/.elf/.bin`；找不到时手动点「选择 programmer」。

4. 点「开始 9008 烧录」，进度条走完后设备自动重启退出 9008。

**风险提示**：9008 烧录会按 XML 声明的起始扇区直接写 eMMC/UFS，写错 XML 或选错机型固件可能变砖；请确认固件与机型、存储类型（emmc/ufs）一致，全程不要断 USB、不要拔线。Fastboot 擦除分区同样不可恢复。



***

## 8. 已验证与未验证

已完成（本机可做的静态检查）：



* 全部新增 `.h/.cpp/.qml` 括号 / 花括号配对检查通过；

* `application/qml.qrc` XML 可解析，`AndroidOverlay.qml` 已注册，qrc 内所有 `file` 均存在；

* `backend/backend.pro` 中列出的每个 `android/*.cpp|h` 均存在；

* Qt5/Qt6 差异点已规避：`QByteArray::size()` 统一 `qint64` 再 `qMin`；`QThread::msleep`（Qt5 为 protected）改用 `QWaitCondition` 延时。

未做（本机没有 Qt / MSVC，无法编译）：



* 未执行 qmake /nmake，未运行程序；

* USB 枚举、WinUSB 读写、Sahara/Firehose 时序、Fastboot 命令均未在真机上跑通；

* QML 面板的实际排版（字号 / 间距 / 各卡片高度）未做可视化校准。

首次编译后建议按以下顺序验证：能启动 → 标签页能切换 → 插手机能看到设备与模式 → Fastboot `getvar` 能回填型号 → 小镜像（如 `boot`）刷写 → 最后再试 9008 全量烧录。



***

## 9. 已知限制



* Fastboot 只实现了 `getvar / download / flash / erase / reboot`，未做 `flashall`、sparse 镜像、A/B 槽位切换、`--slot` 参数。

* 9008 的 `start_sector` 只接受数字；若固件里出现 `NUM_DISK_SECTORS-33.` 这类表达式会直接报错（避免写错扇区），需要先在 XML 里改成数字或换用 QFIL。

* 设备枚举不区分「同一台手机的多功能复合接口」以外的重复项；个别厂商复合设备可能出现两条同型号记录。

* ADB 模式只读信息，不执行 `adb` 命令（避免与用户本机 adb 服务争抢设备）。

* 全部代码为本轮新增，未经编译验证，接口与协议按 Qualcomm Sahara v2 / Firehose 与 AOSP fastboot 规范实现。
## 附：实际编译记录（2026-11-20，本机 Windows 实测）

代码已经不是"仅静态检查"状态了，下面这条路径是真正编译成功并出包的：

- **工具链**：Qt 6.4.2 `win64_mingw`（用 aqtinstall 装的，额外补 `qt5compat`、`qtserialport` 两个模块）+ MinGW 11.2.0（posix-seh），**全程没用 MSVC**，所以不需要装 Visual Studio。
- **nanopb 子模块**：源码包里的 `3rdparty/nanopb` 是空目录（GitHub 压缩包不带子模块），会直接编译失败。已从 `nanopb-0.4.6` 补入 `pb.h / pb_common.* / pb_decode.* / pb_encode.*`（仓库里生成的 `.pb.c` 头版本是 40，必须用 0.4.x）。
- **两处 MinGW 适配（已回写源码）**：
  1. `backend/android/androiddevicescanner.cpp`：MinGW 的 `devpkey.h` 只有声明没有实体，链接期会缺 `DEVPKEY_Device_BusReportedDeviceDesc` 符号 → 改为本地 `kBusReportedDescKey`（GUID `a45c254e-df1c-4efd-8020-67d146a850e0`，pid 4），MSVC 与 MinGW 都可用。
  2. `application/application.pro`、`cli/cli.pro` 末尾各加一段 `win32-g++ { LIBS += -lsetupapi -lwinusb }`：MinGW 静态库链接有顺序要求，`qflipper_common.pri` 里的这一对出现在 `-lbackend -ldfu` 之前，会导致 WinUSB/SetupAPI 符号解析不到。
- **编译命令**（在纯英文路径下做影子构建，源码中文路径容易让 windres 出错）：
  ```
  qmake C:\qf\qflipper.pro -spec win32-g++ CONFIG+=release
  mingw32-make -j8
  ```
  结果：全部子项目通过，产出 `qFlipper.exe`（约 1.9 MB）。
- **运行自检**：以 `-platform offscreen` 启动 7 秒，日志里没有任何 QML 报错，说明 QML 资源、`AndroidBackend` 单例注册、`AndroidOverlay` 实例化都正常。
- **打包**：`windeployqt` 在 MinGW 包里会报 `Unable to find the platform plugin`（包本身缺指向），改用手工收集：`Qt6*.dll` + `libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll` + 整个 `plugins` 与 `qml` 目录 + `qt.conf`（Prefix=./Plugins=plugins/Qml2Imports=qml）。精简掉 Designer/Help/Test/Sql 等无关 DLL 后共 68 MB。
- **仍未验证**：真机 USB 枚举（SetupAPI 分类是否正确）、Fastboot 实际 `download/flash/erase`、9008 实际 Sahara 握手与 Firehose 逐分区写入。这三项必须有设备才能验证，代码里已按协议文档实现但没跑过。
## 补充：第二批功能（中英切换 / iOS 识别 / 机型校验 / 补丁下载）

这一批全部是本轮新增，已编译进 `qFlipper-安卓刷机版\qFlipper.exe`（2050048 B，`-platform offscreen` 启动无 QML 报错）。

### 1. 中英一键切换（含"图案"按钮）

* 不做 `QTranslator` 全量翻译（qm 文件要重跑 lupdate/lrelease，且原 Flipper 面板不需要），改成**属性驱动**：`AndroidBackend.english` 为唯一开关，QML 里所有文案写成 `txt(中文, English)`，绑定随属性变化自动重新求值。
* 按钮位置：`AndroidOverlay.qml` 右上角 `langButton`（x 738 / y 2 / 56×22，胶囊形，鼠标悬停变色），点击调 `AndroidBackend.toggleLanguage()`。中文态边框橙色、英文态边框绿色，按钮内文字 `中文 ⇄` / `EN ⇄`。
* C++ 侧对应实现（`backend/android/androidbackend.cpp`）：
  * `setLanguage(bool)`：设 `m_english` → `emit languageChanged()` → 通过 `requestLanguage(bool)` 把开关投递给工作线程 → 用新语言重算一遍状态条文本 → `updateModelCheck()` → `emit selectedDeviceChanged()`（让 `device.modeName` 跟着换语言）。
  * 私有 `loc(中文, 英文)`；`setStatus` 增加 `(中文, 英文, error=false)` 重载。
* 工作线程侧：`androidworker.h` 加 `setEnglish(bool)` 槽 + `loc()`；`androidworker.cpp` 里 8 处操作结果文案（无法打开 Fastboot 设备 / 已写入分区 / 已擦除分区 / 重启命令已发送 / 烧录完成 / 烧录失败）已全部双语化。
* `androiddevicemodel` 增加 `ModeNameEnRole`（role 名 `modeNameEn`），设备列表的副标题按语言取 `modeName` 或 `modeNameEn`。
* `MainWindow.qml` 标签页文字：`AndroidBackend.english ? "ANDROID" : "安卓刷机"`。
* 默认语言中文，不写配置文件，重开回到中文（如需持久化可存 `QSettings`）。

### 2. iOS 适配（只识别，不刷写）

* `androidtypes.h`：`DeviceMode` 增加 `IosDfu / IosRecovery / IosNormal`，`modeName / modeNameEn / modeKey` 三个函数补对应分支（key 分别是 `iosdfu / iosrecovery / ios`）。
* 新增 `isAppleDevice(vid, pid, &mode)`：苹果 VID `0x05AC`；
  * DFU：`0x1227`、`0x1222`
  * 恢复模式：`0x1280`、`0x1281`、`0x1283`
  * 正常模式：`0x12A8`、`0x12AB`
  * 其它苹果 PID 一律按 `IosNormal` 列出（不丢设备）
* `DeviceInfo::isIos()`；`selectedDevice` 里多一个 `isIos` 键给 QML 用。
* `androiddevicescanner.cpp`：扫到苹果 VID 时**先于**高通/安卓分支判断，填型号（用 USB 描述或序列号前缀）、端口、`detail`，`canFastboot/canEdl` 均为 false。
* `AndroidOverlay.qml`：`actionHint` 增加 iOS 分支，提示"本工具只做识别与信息读取，恢复固件请用 iTunes / 访达或 idevicerestore"。
* 有意不做：iOS 刷机需要 SHSH 校验、checkm8 漏洞机型限制、iTunes 恢复流程，属于另一整套协议栈（`idevicerestore` + `libimobiledevice`），本轮只保证"能看见、能读信息、不会误操作"。

### 3. 机型档案与刷前校验

* `androidtypes.h` 新增 `struct ModelProfile { QString label; QString codename; }` 与 `inline QVector<ModelProfile> modelProfiles()`，内置 14 条：
  `whyred / ginkgo / alioth / marble / cepheus / umi / venus / lemonade / panther / flame`，以及 `荣耀 30 Pro / 华为 P40 / 三星 Galaxy S20 / 其它-不校验` 四条 **codename 为空**（表示只做标识不校验，避免把没核实过的代号写死成规则）。
* 后端新增：`modelProfiles`（QML 下拉数据源）、`profileIndex / profileName / profileCodename`、`modelCheckText`、`modelMismatch`、`Q_INVOKABLE selectProfile(int)`、私有 `updateModelCheck()`。
* 校验逻辑（`updateModelCheck`）：
  1. 当前档案 codename 为空 → 不做校验（`modelCheckText` 显示"当前档案不校验机型"）。
  2. 没选设备 → "等待设备接入"。
  3. 设备 product 还没回填 → "等待读取代号…"。
  4. `product.contains(codename)` → 匹配，绿色文字。
  5. 否则 `m_modelMismatch = true`，红字提示，并在 `fastbootFlash / fastbootErase` 入口直接 `return`（拒绝执行）。
* `onDeviceVarsReady()`（fastboot `getvar` 回填回调）里也会再调一次 `updateModelCheck()`，所以插上设备不用手动刷新。
* QML：信息卡高度 92 → 112，新增一行 `机型：` + `ComboBox`（x 56 / y 66 / 186×24，自绘 background + indicator + delegate，跟随主题配色），右侧显示 `AndroidBackend.modelCheckText`，mismatch 时变红。

### 4. 补丁自动下载

* `backend.pro` 早已 `QT += network`；后端新增成员 `QNetworkAccessManager *m_network`、`QNetworkReply *m_patchReply`，属性 `patchBusy / patchProgress / patchStatus`（统一 `patchChanged()` 通知）。
* `Q_INVOKABLE downloadPatch(QUrl patchUrl, QUrl directoryUrl)`：
  * 并发保护（已在下载则拒绝）；
  * 校验 http/https，非法协议直接报错；
  * 目录为空则用 `QStandardPaths::DownloadLocation`（退 `DocumentsLocation`）；目录不存在则 `mkpath`；
  * 文件名取 URL 末段（`suggestedPatchName`），重名自动加 `_1 … _99` 后缀，**绝不覆盖已有文件**；
  * `NoLessSafeRedirectPolicy`，`downloadProgress` 回调把百分比写进 `m_patchProgress`，状态写"下载中 X / Y"；
  * `finished` 时 `readAll()` 写盘、报告"已保存 文件名（大小）"，失败则写 `errorString`。
* QML：状态条右侧 `patchButton`（"补丁 / Patch"）点击展开 `patchPopup` 浮层（x 470 / y 148 / 316×178，z 5）：地址输入框 + "开始下载" + 进度条 + 状态文字 + 一行说明"只下载保存，不自动写入设备"。
* 有意不做：没有内置服务器/清单，也没有签名校验；下载源由用户自己填。要"自动"到全自动的程度，需要一份可信任的 manifest（机型 → 补丁 URL + 哈希），本轮没编造这种源。

### 本批改动的文件清单

| 文件 | 改动 |
|---|---|
| `backend/android/androidtypes.h` | iOS 三种模式、`isAppleDevice()`、`DeviceInfo::isIos()`、`ModelProfile` + `modelProfiles()` |
| `backend/android/androiddevicescanner.cpp` | 苹果 VID 识别分支（在高通分支之前） |
| `backend/android/androiddevicemodel.h/.cpp` | `ModeNameEnRole` / `modeNameEn` |
| `backend/android/androidbackend.h` | 语言、机型、补丁三组属性/信号/槽 + `setStatus` 双语重载 |
| `backend/android/androidbackend.cpp` | 上述全部实现（`loc / setLanguage / selectProfile / updateModelCheck / downloadPatch / suggestedPatchName`） |
| `backend/android/androidworker.h/.cpp` | `setEnglish` 槽 + 8 处操作文案双语 |
| `application/components/AndroidOverlay.qml` | 语言按钮、机型下拉、iOS 提示、补丁浮层、全页文案 `txt(中,英)`、信息卡 92→112 |
| `application/components/MainWindow.qml` | 标签页文字随语言切换 |

### 编译与打包（Windows，MinGW 路线，和第一批一致）

```bat
:: 前提：Qt 6.4.2 mingw_64 + Qt5Compat + SerialPort + MinGW 11.2 已装好
:: 源码放在纯英文路径，例如 C:\qf
qmake C:\qf\qflipper.pro -spec win32-g++ CONFIG+=release
mingw32-make -j8
:: 产物：C:\qf-build\qFlipper.exe
```

* 只改 QML 时 `mingw32-make` 会重跑 `rcc`，一条命令即可；改了 `.pro` 或新增文件要重新 `qmake`。
* 打包：把新 `qFlipper.exe` 覆盖到 `qFlipper-安卓刷机版\` 即可，Qt 运行库/plugins/qml/qt.conf 不用动。
* 本批踩到的两个坑，留档备查：
  1. `list.append({ QStringLiteral("x"), QString() })` 在 MinGW GCC 11 下报 `expected '}' before ')'`，改成 `QStringLiteral("")` 即可。
  2. 成员函数 `AndroidBackend::modelProfiles()` 会遮蔽同名自由函数，类内调用必须写 `AndroidFlasher::modelProfiles()`。
  3. QML 里函数名不能大写开头（`function L()` 直接报 `Method names cannot begin with an upper case letter`），已改名 `txt()`。

## 第三批改动（32 位构建 + Fastboot 五目标 + 苹果芯片识别）

### 新增 / 修改的代码位置

| 文件 | 改动 |
|---|---|
| `backend/android/fastbootclient.cpp` | `reboot()` 改为 `QStringList` 多命令重试；新增 `edl` 目标：先发 `reboot edl`，FAIL 再发 `oem edl`；补 `#include <QStringList>` |
| `backend/android/androidtypes.h` | 新增 `appleChipName(int cpid)`（CPID → A11/A12/A13…）与 `appleParseCpid()`（从 DFU/恢复序列号或实例 ID 里抠 CPID） |
| `backend/android/androiddevicescanner.cpp` | 苹果分支追加芯片识别：`product` 变成 `iPhone / iPad · A12 (CPID 0x8020)`，`detail` 追加 DFU / Recovery / Normal |
| `application/components/AndroidOverlay.qml` | Fastboot 的 reboot 行改为四键：桌面 / BOOTLOADER / RECOVERY / 9008，x=76/148/284/394 |
| `qflipper_common.pri` | `win32` 段新增 `DEFINES += _WIN32_WINNT=0x0601 WINVER=0x0601`（32 位 MinGW 8.1 默认值太低，`CancelIoEx` / `SetupDiGetDevicePropertyW` 声明不进来） |

### 32 位构建（Qt 5.15.2 + MinGW 8.1）

Qt 6 全系列没有官方 32 位桌面构建，32 位只能走 Qt 5.15：

```bat
python -m aqt install-qt windows desktop 5.15.2 win32_mingw81 -O C:\Qt5
python -m aqt install-tool windows desktop tools_mingw81 -O C:\Qt5
set PATH=C:\Qt5\5.15.2\mingw81_32\bin;C:\Qt5\Tools\mingw810_32\bin;%PATH%
qmake C:\qf\qflipper.pro -spec win32-g++ CONFIG+=release   :: 在 C:\qf-build32 里执行
mingw32-make -j8
```

* 工程本身对 Qt5/Qt6 兼容（`qflipper_common.pri` 里 `core5compat` 只在 Qt6 加），QML 用的是 2.12/2.15 版本号，Qt 5.15 可以直接跑。
* 32 位的 `windeployqt` 同样会报 `Unable to find the platform plugin`，只能手工拷：`Qt5*.dll` + `plugins\{platforms,imageformats,iconengines,styles,sqldrivers}` + `qml\{QtQuick,QtQuick.2,QtQml,QtGraphicalEffects,Qt}` + `qt.conf`。
* Qt 5.15 的 HTTPS 依赖 OpenSSL 1.1（`libssl-1_1.dll` / `libcrypto-1_1.dll`），本机没有随附，所以 32 位版的 https 下载会报 `TLS initialization failed`，用 http 或改用 64 位版下载补丁。
* 32 位 exe 用 `-platform offscreen` 可以离屏跑起来做冒烟测试（只会打印找不到字体目录的警告，无害）。

### 32/64 合一安装包的做法（最终采用官方 systeminfo 方式）

Qt Installer Framework 的做法参照自带 `systeminfo` 示例：两个架构包默认都 `<Virtual>true</Virtual>`（隐藏），
根组件在 `Component()` 里按 `systemInfo.currentCpuArchitecture` 放开其中一个，装出来只有一套程序，
目录和官方包一样干净（`qFlipper.exe` 平铺在根）。

```js
var arch = systemInfo.currentCpuArchitecture;                 // x86_64 / i386
var want64 = (arch.indexOf("64") >= 0 || arch.indexOf("amd64") >= 0);
var c64 = installer.componentByName("com.flipperdevices.qflipper.x64");
var c32 = installer.componentByName("com.flipperdevices.qflipper.x86");
if (want64) { c64.setValue("Virtual", "false"); c64.setValue("Default", "true"); }
else        { c32.setValue("Virtual", "false"); c32.setValue("Default", "true"); }
```

注意：键名必须是 `Virtual` / `Default`。早先试过 `setValue("checked", ...)`，不生效，两个包会一起装进同一目录。

包结构：

```
packages/
  com.flipperdevices.qflipper/       根组件：使用说明 + 补丁 + 快捷方式脚本
  com.flipperdevices.qflipper.x64/   64 位程序（<Virtual>true</Virtual>）
  com.flipperdevices.qflipper.x86/   32 位程序（<Virtual>true</Virtual>）
```

打包：

```bat
binarycreator.exe -c config.xml -p packages --offline-only qFlipper-1.0.0-win-32-64.exe
```

验证（headless，装到临时目录后看是 Qt6*.dll 还是 Qt5*.dll，再看 components.xml 装的是哪个组件）：

```bat
qFlipper-1.0.0-win-32-64.exe in -t C:\tmp --al --am -c
```

`tools/launcher/` 里的 32 位启动器是早期方案（app64/app32 + 启动器），现在保留作备用，安装包不再使用它。


