qFlipper 1.0.0 · 补丁包（Patch）
================================================

这个文件夹是给主程序配的「离线补丁」，不装也不影响主程序，装了更省事。
This folder is the offline patch bundle for the main app. The app runs without it; with it things are easier.

里面有三样东西 / Three things inside:

1) 切换语言（Switch-Language）.bat —— 中英界面切换开关
   Toggles the Chinese / English UI permanently.

   · 双击运行：原本中文 → 变成英文；再双击一次 → 回到中文。
     Double-click: Chinese -> English; run again -> back to Chinese.
   · 原理：在程序目录（上一级）里 创建 / 删除 一个名为 english.flag 的空文件。
     How it works: it creates or deletes an empty file named english.flag next to qFlipper.exe.
   · 改完要重新打开 qFlipper 才生效（程序启动时读这个标记）。
     Restart qFlipper afterwards; the flag is read at startup.
   · 主界面右上角的小圆钮也能切，但那只对本次运行有效，重开就回中文。
     The pill button in the upper-right corner also switches, but only for the current run.

2) 安装驱动（Install-Driver）.bat —— 一键装安卓刷机用的 USB 驱动
   One-click install of the USB driver used for Android flashing.

   · 需要右键「以管理员身份运行」。
     Must be run as administrator (right-click -> Run as administrator).
   · 装的是 drivers\google-usb 里的 Google USB Driver，覆盖 ADB 与 Fastboot 接口。
     Installs the Google USB Driver in drivers\google-usb, which covers ADB and Fastboot.
   · 运行完拔插一次手机，设备管理器里应该出现 Android ADB Interface / Android Bootloader Interface。
     Replug the phone afterwards; Device Manager should show Android ADB Interface / Android Bootloader Interface.

3) 驱动说明（Driver-Guide）.txt —— 三种模式的驱动怎么装（含高通 9008 与苹果）
   Driver guide for the three modes, including Qualcomm 9008 and Apple devices.

注意 / Note
- 高通 9008（QDL）用的是串口驱动 Qualcomm HS-USB QDLoader 9008，不在本包内（授权不允许再分发），
  说明文件里给了获取方式和识别办法。
  The Qualcomm 9008 (QDL) mode needs the Qualcomm HS-USB QDLoader 9008 serial driver, which is not
  bundled here (its license forbids redistribution). See the guide for where to get it.
- 本补丁只装驱动、只切语言，不改动主程序任何文件。
  This patch only installs drivers and flips the language flag; it never modifies the app itself.
