import QtQuick 2.15
import QtQuick.Controls 2.15

import QFlipper 1.0
import Theme 1.0

// [ANDROID-FLASHER] 新增文件：安卓刷机面板（第二个标签页的全部界面）
// 与 Flipper 面板互斥显示：由 MainWindow.qml 通过 visible 控制。
// 本次扩展：右上角语言切换按钮（中/EN）、机型选择与校验、iOS 识别提示、补丁下载浮层。

Item {
    id: control

    readonly property var device: AndroidBackend.selectedDevice
    readonly property string selectedPath: device && device.path ? device.path : ""

    property url imageUrl: ""
    property url rawProgramUrl: ""
    property url programmerUrl: ""
    property var rawProgramInfo: ({})

    readonly property string chineseFont: Qt.platform.os === "windows" ? "Microsoft YaHei UI" : "HaxrCorp 4089"
    readonly property bool english: AndroidBackend.english

    // [ANDROID-FLASHER] 所有界面文案都用这个函数二选一，切换语言时自动重新求值
    function txt(zh, en) {
        return control.english ? en : zh
    }

    function baseName(fileUrl) {
        if(!fileUrl) {
            return "";
        }

        const text = fileUrl.toString();
        const parts = text.split("/");

        return parts.length ? parts[parts.length - 1] : text;
    }

    function hasUrl(fileUrl) {
        return fileUrl ? fileUrl.toString() !== "" : false;
    }

    function modeColor(modeKey) {
        if(modeKey === "fastboot") {
            return Theme.color.lightblue;
        }

        if(modeKey === "qdl") {
            return Theme.color.lightred1;
        }

        if(modeKey === "iosdfu" || modeKey === "iosrecovery" || modeKey === "ios") {
            return Theme.color.lightgreen;
        }

        return Theme.color.lightorange2;
    }

    // [ANDROID-FLASHER] 机型档案的显示名列表（供下拉框使用）
    function profileLabels() {
        const list = AndroidBackend.modelProfiles;
        const out = [];

        for(let i = 0; i < list.length; ++i) {
            out.push(list[i].label);
        }

        return out;
    }

    function chooseImage() {
        SystemFileDialog.accepted.connect(function() {
            control.imageUrl = SystemFileDialog.fileUrl;
        });

        SystemFileDialog.beginOpenFile(SystemFileDialog.LastLocation,
                                       ["镜像文件 (*.img *.bin *.raw)", "所有文件 (*.*)"]);
    }

    function chooseRawProgram() {
        SystemFileDialog.accepted.connect(function() {
            control.rawProgramUrl = SystemFileDialog.fileUrl;
            control.rawProgramInfo = AndroidBackend.inspectRawProgram(control.rawProgramUrl);
        });

        SystemFileDialog.beginOpenFile(SystemFileDialog.LastLocation,
                                       ["rawprogram 文件 (*.xml)", "所有文件 (*.*)"]);
    }

    function chooseProgrammer() {
        SystemFileDialog.accepted.connect(function() {
            control.programmerUrl = SystemFileDialog.fileUrl;
        });

        SystemFileDialog.beginOpenFile(SystemFileDialog.LastLocation,
                                       ["programmer 文件 (*.mbn *.elf *.bin)", "所有文件 (*.*)"]);
    }

    // 顶部标题
    Text {
        x: 16
        y: 6

        text: "ANDROID FLASHER"
        color: Theme.color.lightorange2

        font.family: "HaxrCorp 4089"
        font.pixelSize: 18
    }

    Text {
        x: 150
        y: 10
        width: 430

        text: control.txt("USB 自动扫描 · Fastboot 刷机 · 高通 9008 烧录 · iOS 识别",
                        "USB auto scan · Fastboot · Qualcomm 9008 · iOS detect")
        color: Theme.color.mediumorange1
        elide: Text.ElideRight

        font.family: control.chineseFont
        font.pixelSize: 12
    }

    // [ANDROID-FLASHER] 语言切换按钮：点一下在中/英之间切换（整页文案一起变）
    Rectangle {
        id: langButton

        x: 604
        y: 2
        width: 56
        height: 22

        radius: 11
        color: langMouse.containsMouse ? "#3a1d00" : "#241000"
        border.width: 1
        border.color: control.english ? Theme.color.lightgreen : Theme.color.lightorange2

        MouseArea {
            id: langMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: AndroidBackend.toggleLanguage()
        }

        Text {
            anchors.centerIn: parent

            text: control.english ? "EN ⇄" : "中文 ⇄"
            color: control.english ? Theme.color.lightgreen : Theme.color.lightorange2

            font.family: control.chineseFont
            font.pixelSize: 11
        }

        Text {
            x: -96
            y: 24
            width: 200
            horizontalAlignment: Text.AlignRight
            visible: langMouse.containsMouse

            text: control.txt("点击切换中文 / 英文", "Click to switch 中文 / English")
            color: Theme.color.mediumorange1

            font.family: control.chineseFont
            font.pixelSize: 10
        }
    }

    // 左侧设备列表
    Rectangle {
        id: devicePanel

        x: 14
        y: 32
        width: 268
        height: 300

        radius: 4
        color: "#1b0e02"
        border.width: 1
        border.color: Theme.color.mediumorange3

        Text {
            x: 10
            y: 8

            text: control.english ? "Devices (" + AndroidBackend.deviceCount + ")" :
                                    "设备列表 (" + AndroidBackend.deviceCount + ")"
            color: Theme.color.lightorange2

            font.family: control.chineseFont
            font.pixelSize: 13
        }

        ListView {
            id: deviceList

            x: 6
            y: 30
            width: parent.width - 12
            height: parent.height - 70

            clip: true
            spacing: 3
            boundsBehavior: Flickable.StopAtBounds
            model: AndroidBackend.deviceModel

            delegate: Rectangle {
                width: ListView.view.width
                height: 52
                radius: 3

                color: control.selectedPath === path ? Theme.color.darkorange1 :
                       (deviceMouse.containsMouse ? "#2a1400" : "#241000")
                border.width: 1
                border.color: control.selectedPath === path ? Theme.color.lightorange2 : Theme.color.mediumorange2

                MouseArea {
                    id: deviceMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: AndroidBackend.selectDevice(index)
                }

                Text {
                    x: 8
                    y: 5
                    width: parent.width - 16

                    text: product
                    color: Theme.color.lightorange2
                    elide: Text.ElideRight

                    font.family: "Share Tech"
                    font.pixelSize: 13
                }

                Text {
                    x: 8
                    y: 22

                    text: control.english ? modeNameEn : modeName
                    color: control.modeColor(modeKey)

                    font.family: control.chineseFont
                    font.pixelSize: 12
                }

                Text {
                    x: 8
                    y: 37
                    width: parent.width - 16

                    text: (port ? port + " · " : "") + vidPid
                    color: Theme.color.mediumorange1
                    elide: Text.ElideRight

                    font.family: "Share Tech"
                    font.pixelSize: 11
                }

                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }
                }
            }
        }

        Text {
            x: 10
            y: 60
            width: parent.width - 20
            visible: AndroidBackend.deviceCount === 0

            text: control.txt("未检测到设备\n\n请连接手机并：\n· 开启 USB 调试（普通安卓）\n· 重启到 Bootloader（Fastboot）\n· 关机后按住音量键接入（9008）\n· iPhone/iPad 进入 DFU 或恢复模式（仅识别）",
                            "No device found\n\nConnect a device and:\n· enable USB debugging (Android)\n· reboot to bootloader (Fastboot)\n· hold volume keys while plugging (9008)\n· put iPhone/iPad into DFU or Recovery (detect only)")
            color: Theme.color.mediumorange1

            font.family: control.chineseFont
            font.pixelSize: 11
            lineHeight: 1.4
            wrapMode: Text.WordWrap
        }

        SmallButton {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 8

            width: 92
            height: 28

            text: control.txt("刷新", "Refresh")
            font.family: control.chineseFont
            font.pixelSize: 14
            font.capitalization: Font.MixedCase
            padding: 2
            borderWidth: 1

            onClicked: AndroidBackend.refreshDevices()
        }

        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10

            text: control.txt("自动刷新中", "auto refresh")
            color: Theme.color.mediumorange1

            font.family: control.chineseFont
            font.pixelSize: 10
        }
    }

    // 选中设备信息（高度 112：新增“目标机型”一行）
    Rectangle {
        id: infoPanel

        x: 294
        y: 32
        width: 492
        height: 112

        radius: 4
        color: "#1b0e02"
        border.width: 1
        border.color: Theme.color.mediumorange3

        Text {
            x: 12
            y: 6
            width: parent.width - 24

            text: control.device && control.device.product ? control.device.product :
                  control.txt("未选择设备", "no device selected")
            color: Theme.color.lightorange2
            elide: Text.ElideRight

            font.family: "Share Tech"
            font.pixelSize: 17
        }

        Text {
            x: 12
            y: 30

            text: control.device && control.device.modeName ? control.device.modeName : "—"
            color: control.modeColor(control.device ? control.device.modeKey : "")

            font.family: control.chineseFont
            font.pixelSize: 13
        }

        Text {
            x: 180
            y: 30

            text: control.txt("端口：", "port: ") + (control.device && control.device.port ? control.device.port : "—")
            color: Theme.color.lightorange2

            font.family: control.chineseFont
            font.pixelSize: 12
        }

        Text {
            x: 320
            y: 30

            text: "VID:PID：" + (control.device && control.device.vidPid ? control.device.vidPid : "—")
            color: Theme.color.lightorange2

            font.family: control.chineseFont
            font.pixelSize: 12
        }

        Text {
            x: 12
            y: 50
            width: parent.width - 24

            text: control.txt("序列号：", "serial: ") + (control.device && control.device.serial ? control.device.serial : "—")
            color: Theme.color.mediumorange1
            elide: Text.ElideRight

            font.family: "Share Tech"
            font.pixelSize: 11
        }

        // [ANDROID-FLASHER] 机型档案下拉 + 校验结果
        Text {
            x: 12
            y: 70

            text: control.txt("机型：", "model: ")
            color: Theme.color.lightorange2

            font.family: control.chineseFont
            font.pixelSize: 12
        }

        ComboBox {
            id: profileBox

            x: 56
            y: 66
            width: 186
            height: 24

            model: control.profileLabels()
            currentIndex: AndroidBackend.profileIndex

            onActivated: AndroidBackend.selectProfile(index)

            font.family: control.chineseFont
            font.pixelSize: 11

            background: Rectangle {
                radius: 3
                color: "#241000"
                border.width: 1
                border.color: Theme.color.mediumorange2
            }

            contentItem: Text {
                leftPadding: 6
                rightPadding: 16

                text: profileBox.displayText
                color: Theme.color.lightorange2
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter

                font: profileBox.font
            }

            indicator: Text {
                x: profileBox.width - 16
                y: 4

                text: "▾"
                color: Theme.color.lightorange2

                font.family: "Share Tech"
                font.pixelSize: 12
            }

            delegate: ItemDelegate {
                width: profileBox.width
                height: 24

                contentItem: Text {
                    leftPadding: 6

                    text: modelData
                    color: Theme.color.lightorange2
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter

                    font.family: control.chineseFont
                    font.pixelSize: 11
                }

                background: Rectangle {
                    color: highlighted ? Theme.color.darkorange1 : "#241000"
                }
            }
        }

        Text {
            x: 250
            y: 70
            width: parent.width - 262

            text: AndroidBackend.modelCheckText
            color: AndroidBackend.modelMismatch ? Theme.color.lightred1 : Theme.color.mediumorange1
            elide: Text.ElideRight

            font.family: control.chineseFont
            font.pixelSize: 10
        }

        Text {
            x: 12
            y: 94
            width: parent.width - 24

            text: control.device && control.device.detail ? control.device.detail : ""
            color: control.device && control.device.hasWinUsb === false ? Theme.color.lightred1 : Theme.color.mediumorange1
            elide: Text.ElideRight

            font.family: control.chineseFont
            font.pixelSize: 10
        }
    }

    // 操作区
    Rectangle {
        id: actionPanel

        x: 294
        y: 150
        width: 492
        height: 182

        radius: 4
        color: "#1b0e02"
        border.width: 1
        border.color: Theme.color.mediumorange3

        // ---------- Fastboot ----------
        Item {
            id: fastbootPane
            anchors.fill: parent
            visible: control.device && control.device.canFastboot === true

            Text {
                x: 12
                y: 8

                text: control.txt("分区", "partition")
                color: Theme.color.lightorange2

                font.family: control.chineseFont
                font.pixelSize: 13
            }

            TextField {
                id: partitionField

                x: 76
                y: 4
                width: 100
                height: 30

                placeholderText: "boot"
                text: "boot"
                color: Theme.color.lightorange2
                placeholderTextColor: Theme.color.mediumorange1
                verticalAlignment: TextInput.AlignVCenter

                font.family: "Share Tech"
                font.pixelSize: 15

                background: Rectangle {
                    radius: 3
                    color: "#241000"
                    border.width: 1
                    border.color: Theme.color.mediumorange2
                }
            }

            SmallButton {
                x: 182
                y: 4
                width: 128
                height: 30

                text: control.txt("选择镜像", "Select image")
                font.family: control.chineseFont
                font.pixelSize: 12
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 1

                onClicked: control.chooseImage()
            }

            Text {
                x: 318
                y: 10
                width: 168

                text: control.hasUrl(control.imageUrl) ? control.baseName(control.imageUrl) :
                                                        control.txt("未选择镜像", "no image")
                color: control.hasUrl(control.imageUrl) ? Theme.color.lightorange2 : Theme.color.mediumorange1
                elide: Text.ElideMiddle

                font.family: "Share Tech"
                font.pixelSize: 12
            }

            SmallButton {
                x: 12
                y: 44
                width: 150
                height: 32

                text: control.txt("刷入镜像", "Flash image")
                enabled: !AndroidBackend.isBusy
                font.family: control.chineseFont
                font.pixelSize: 13
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 1

                onClicked: AndroidBackend.fastbootFlash(partitionField.text, control.imageUrl)
            }

            SmallButtonRed {
                x: 174
                y: 44
                width: 150
                height: 32

                text: control.txt("擦除分区", "Erase partition")
                enabled: !AndroidBackend.isBusy
                font.family: control.chineseFont
                font.pixelSize: 13
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 1

                onClicked: AndroidBackend.fastbootErase(partitionField.text)
            }

            Text {
                x: 12
                y: 84

                text: control.txt("重启：", "reboot: ")
                color: Theme.color.lightorange2

                font.family: control.chineseFont
                font.pixelSize: 13
            }

            SmallButton {
                x: 76
                y: 80
                width: 66
                height: 28

                text: control.txt("桌面", "system")
                enabled: !AndroidBackend.isBusy
                font.family: control.chineseFont
                font.pixelSize: 12
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 1

                onClicked: AndroidBackend.fastbootReboot("system")
            }

            SmallButton {
                x: 148
                y: 80
                width: 130
                height: 28

                text: "BOOTLOADER"
                enabled: !AndroidBackend.isBusy
                font.family: "Share Tech"
                font.pixelSize: 12
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 1

                onClicked: AndroidBackend.fastbootReboot("bootloader")
            }

            SmallButton {
                x: 284
                y: 80
                width: 104
                height: 28

                text: "RECOVERY"
                enabled: !AndroidBackend.isBusy
                font.family: "Share Tech"
                font.pixelSize: 12
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 1

                onClicked: AndroidBackend.fastbootReboot("recovery")
            }

            // [ANDROID-FLASHER] 一键重启到高通 9008（自动尝试 reboot edl → oem edl）
            SmallButton {
                x: 394
                y: 80
                width: 60
                height: 28

                text: "9008"
                enabled: !AndroidBackend.isBusy
                font.family: "Share Tech"
                font.pixelSize: 12
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 1

                onClicked: AndroidBackend.fastbootReboot("edl")
            }

            Text {
                x: 12
                y: 116
                width: parent.width - 24

                text: control.txt("提示：镜像必须与机型匹配；Bootloader 需解锁，Windows 需为设备安装 WinUSB / Google USB 驱动。9008 按钮会先试 reboot edl，不行再试 oem edl。",
                                "Note: images must match the model; bootloader must be unlocked; install WinUSB / Google USB Driver on Windows. The 9008 button tries \"reboot edl\" first, then \"oem edl\".")
                color: Theme.color.mediumorange1
                wrapMode: Text.WordWrap

                font.family: control.chineseFont
                font.pixelSize: 10
                lineHeight: 1.3
            }
        }

        // ---------- 高通 9008 ----------
        Item {
            id: edlPane
            anchors.fill: parent
            visible: control.device && control.device.canEdl === true

            SmallButton {
                x: 12
                y: 6
                width: 150
                height: 30

                text: control.txt("选择 XML", "Select XML")
                font.family: control.chineseFont
                font.pixelSize: 12
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 1

                onClicked: control.chooseRawProgram()
            }

            Text {
                x: 174
                y: 6
                width: parent.width - 186

                text: control.hasUrl(control.rawProgramUrl) ? control.baseName(control.rawProgramUrl) :
                                                              "rawprogram0.xml / rawprogram1.xml …"
                color: control.hasUrl(control.rawProgramUrl) ? Theme.color.lightorange2 : Theme.color.mediumorange1
                elide: Text.ElideMiddle

                font.family: "Share Tech"
                font.pixelSize: 12
            }

            Text {
                x: 12
                y: 38
                width: parent.width - 24

                text: control.rawProgramInfo && control.rawProgramInfo.message ?
                      control.rawProgramInfo.message :
                      control.txt("选择固件目录里的 rawprogram 文件，程序会自动统计分区数量",
                                "Pick rawprogram from the firmware folder; partitions are counted automatically")
                color: control.rawProgramInfo && control.rawProgramInfo.valid ? Theme.color.lightgreen : Theme.color.mediumorange1
                elide: Text.ElideRight

                font.family: control.chineseFont
                font.pixelSize: 11
            }

            SmallButton {
                x: 12
                y: 60
                width: 150
                height: 28

                text: control.txt("选择 programmer", "Select programmer")
                font.family: control.chineseFont
                font.pixelSize: 11
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 1

                onClicked: control.chooseProgrammer()
            }

            Text {
                x: 174
                y: 64
                width: parent.width - 186

                text: control.hasUrl(control.programmerUrl) ? control.baseName(control.programmerUrl) :
                                                              control.txt("留空则自动查找 prog_firehose_*.mbn",
                                                                        "empty = auto find prog_firehose_*.mbn")
                color: control.hasUrl(control.programmerUrl) ? Theme.color.lightorange2 : Theme.color.mediumorange1
                elide: Text.ElideMiddle

                font.family: control.chineseFont
                font.pixelSize: 11
            }

            SmallButton {
                x: 12
                y: 94
                width: 190
                height: 34

                text: control.txt("开始 9008 烧录", "Start 9008 flash")
                enabled: !AndroidBackend.isBusy
                font.family: control.chineseFont
                font.pixelSize: 13
                font.capitalization: Font.MixedCase
                padding: 2
                borderWidth: 2

                onClicked: AndroidBackend.edlFlash(control.rawProgramUrl, control.programmerUrl)
            }

            Text {
                x: 12
                y: 132
                width: parent.width - 24

                text: control.txt("烧录过程中请勿断开 USB；完成后设备会自动重启退出 9008。若失败，请重新插拔后再试。",
                                "Do not unplug during flashing; the device reboots out of 9008 when done. On failure, replug and retry.")
                color: Theme.color.mediumorange1
                wrapMode: Text.WordWrap

                font.family: control.chineseFont
                font.pixelSize: 10
                lineHeight: 1.4
            }
        }

        // ---------- 普通安卓 / iOS / 未选择 ----------
        Text {
            id: actionHint
            anchors.fill: parent
            anchors.margins: 16

            visible: !fastbootPane.visible && !edlPane.visible

            text: {
                if(!control.device || !control.device.modeKey) {
                    return control.txt("请在左侧列表中选择一台设备。", "Select a device in the list.");
                }

                if(control.device.isIos === true) {
                    return control.txt("已识别为 iOS 设备（DFU / 恢复模式 / 正常模式）。\n\n本工具只做识别与信息读取，不做 iOS 刷机；恢复固件请使用 iTunes / 访达，或 idevicerestore。",
                                     "iOS device detected (DFU / Recovery / Normal).\n\nThis tool only identifies it — no iOS flashing. Use iTunes / Finder or idevicerestore to restore firmware.");
                }

                if(control.device.modeKey === "adb") {
                    return control.txt("当前是普通安卓模式（ADB/MTP），该模式不支持刷机。\n\n请执行 adb reboot bootloader 进入 Fastboot，或关机后按住音量键连接 USB 进入 9008 模式。",
                                     "Device is in normal Android mode (ADB/MTP); flashing is unavailable.\n\nRun adb reboot bootloader for Fastboot, or hold volume keys while plugging in for 9008.");
                }

                return control.txt("该设备的接口信息不完整，暂时无法执行刷机操作。",
                                 "The device interface info is incomplete; flashing is unavailable.");
            }

            color: Theme.color.mediumorange1
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter

            font.family: control.chineseFont
            font.pixelSize: 12
            lineHeight: 1.5
        }
    }

    // 底部进度与状态
    Rectangle {
        id: statusPanel

        x: 14
        y: 340
        width: 772
        height: 42

        radius: 4
        color: "#1b0e02"
        border.width: 1
        border.color: Theme.color.mediumorange3

        Rectangle {
            id: progressTrack

            x: 8
            y: 6
            width: parent.width - 16
            height: 5

            radius: 2
            color: "#2b1600"
            visible: AndroidBackend.isBusy || AndroidBackend.progress >= 0

            Rectangle {
                height: parent.height
                radius: parent.radius
                color: AndroidBackend.statusIsError ? Theme.color.mediumred1 : Theme.color.lightorange2

                width: AndroidBackend.progress >= 0 ?
                       parent.width * Math.max(0, Math.min(1, AndroidBackend.progress / 100)) :
                       parent.width * 0.18

                Behavior on width {
                    NumberAnimation {
                        duration: 120
                    }
                }
            }
        }

        Text {
            x: 8
            y: 15
            width: parent.width - 110

            text: AndroidBackend.statusText
            color: AndroidBackend.statusIsError ? Theme.color.lightred1 : Theme.color.lightorange2
            elide: Text.ElideRight

            font.family: control.chineseFont
            font.pixelSize: 12
        }

        Text {
            x: 8
            y: 30
            width: parent.width - 110

            text: {
                const lines = AndroidBackend.logText.split("\n");
                return lines.length ? lines[lines.length - 1] : "";
            }
            color: Theme.color.mediumorange1
            elide: Text.ElideRight

            font.family: "Share Tech"
            font.pixelSize: 9
        }

        // [ANDROID-FLASHER] 补丁下载入口（点开右侧浮层）
        SmallButton {
            id: patchButton

            x: parent.width - 96
            y: 8
            width: 84
            height: 26

            text: control.txt("补丁", "Patch")
            font.family: control.chineseFont
            font.pixelSize: 11
            font.capitalization: Font.MixedCase
            padding: 2
            borderWidth: 1

            onClicked: patchPopup.visible = !patchPopup.visible
        }
    }

    // [ANDROID-FLASHER] 补丁自动下载浮层（不自动写入设备，只负责下载保存）
    Rectangle {
        id: patchPopup

        x: 470
        y: 148
        width: 316
        height: 178
        z: 5

        visible: false
        radius: 4
        color: "#241000"
        border.width: 1
        border.color: Theme.color.lightorange2

        Text {
            x: 10
            y: 6

            text: control.txt("补丁自动下载", "Auto patch download")
            color: Theme.color.lightorange2

            font.family: control.chineseFont
            font.pixelSize: 13
        }

        Text {
            x: parent.width - 26
            y: 4
            width: 20

            text: "×"
            color: Theme.color.lightorange2
            horizontalAlignment: Text.AlignHCenter

            font.family: "Share Tech"
            font.pixelSize: 16

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: patchPopup.visible = false
            }
        }

        Text {
            x: 10
            y: 30

            text: control.txt("地址", "URL")
            color: Theme.color.mediumorange1

            font.family: control.chineseFont
            font.pixelSize: 11
        }

        TextField {
            id: patchUrlField

            x: 46
            y: 26
            width: parent.width - 56
            height: 26

            placeholderText: "https://example.com/patch.zip"
            color: Theme.color.lightorange2
            placeholderTextColor: Theme.color.mediumorange1
            verticalAlignment: TextInput.AlignVCenter

            font.family: "Share Tech"
            font.pixelSize: 12

            background: Rectangle {
                radius: 3
                color: "#1b0e02"
                border.width: 1
                border.color: Theme.color.mediumorange2
            }
        }

        SmallButton {
            x: 10
            y: 60
            width: 96
            height: 28

            text: AndroidBackend.patchBusy ? control.txt("下载中", "Busy") :
                                            control.txt("开始下载", "Download")
            enabled: !AndroidBackend.patchBusy
            font.family: control.chineseFont
            font.pixelSize: 12
            font.capitalization: Font.MixedCase
            padding: 2
            borderWidth: 1

            onClicked: AndroidBackend.downloadPatch(patchUrlField.text, "")
        }

        Text {
            x: 116
            y: 62
            width: parent.width - 126

            text: control.txt("默认保存到「下载」文件夹", "saved into the Downloads folder")
            color: Theme.color.mediumorange1
            elide: Text.ElideRight

            font.family: control.chineseFont
            font.pixelSize: 10
        }

        Rectangle {
            x: 10
            y: 94
            width: parent.width - 20
            height: 6

            radius: 3
            color: "#3a1d00"
            visible: AndroidBackend.patchBusy || AndroidBackend.patchProgress >= 0

            Rectangle {
                height: parent.height
                radius: parent.radius
                color: AndroidBackend.patchProgress < 0 ? Theme.color.mediumred1 : Theme.color.lightgreen

                width: AndroidBackend.patchProgress >= 0 ?
                       parent.width * Math.max(0, Math.min(1, AndroidBackend.patchProgress / 100)) :
                       parent.width * 0.2

                Behavior on width {
                    NumberAnimation {
                        duration: 120
                    }
                }
            }
        }

        Text {
            x: 10
            y: 104
            width: parent.width - 20

            text: AndroidBackend.patchStatus
            color: AndroidBackend.patchProgress < 0 && !AndroidBackend.patchBusy ?
                   Theme.color.lightred1 : Theme.color.lightorange2
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 2

            font.family: control.chineseFont
            font.pixelSize: 10
            lineHeight: 1.3
        }

        Text {
            x: 10
            y: 152
            width: parent.width - 20

            text: control.txt("只下载保存，不自动写入设备；刷入仍需在上面选择镜像手动执行。",
                            "Download only — flashing is still manual via the image picker above.")
            color: Theme.color.mediumorange1
            wrapMode: Text.WordWrap

            font.family: control.chineseFont
            font.pixelSize: 9
            lineHeight: 1.3
        }
    }
}
