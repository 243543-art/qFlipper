qFlipper · 安卓刷机版  改装包 / Modded Bundle
=======================================================

文件夹里有什么 / Contents
-------------------------------------------------------
qFlipper\                    64 位便携版（双击里面的 qFlipper.exe 就能跑）
                             Portable 64-bit build (run qFlipper.exe inside)
qFlipper-32bit\              32 位便携版（老电脑/32 位系统用）
                             Portable 32-bit build (for old or 32-bit Windows)
安装包（Installer）\          32+64 位合一安装包，装一次自动选位数
                             Combined 32/64-bit setup, picks the right build on install
使用说明（中文）.txt          中文使用说明
使用说明（English）.txt      English user guide
补丁（Patch）\               语言切换 + 刷机 USB 驱动 + 驱动说明
                             Language switch + USB drivers + driver guide
源码（Source）\              完整源码（保持原目录结构，新增代码在 backend\android\ 与 application\components\）
                             Full source, original folder layout kept

安装包装完长这样 / What the installer produces
-------------------------------------------------------
装的时候安装程序会自己判断你是 64 位还是 32 位系统，只装对应的一套（和官方安装包一样干净）：

安装目录\                        （默认 用户目录\qFlipper）
  qFlipper.exe                   程序本体，直接双击
  Qt6*.dll / plugins / qml       64 位运行库（32 位系统上这里是 Qt5*.dll）
  qt.conf
  使用说明（中文/English）.txt
  补丁（Patch）\
桌面和开始菜单都会有 qFlipper 快捷方式，没有 app64/app32 这种多余目录。
The installer reads your Windows architecture and installs only that one build, exactly like the
official package: a single flat program folder plus the docs and the patch folder.


三点提醒 / Three notes
-------------------------------------------------------
1. 32 位版没有随附 OpenSSL 1.1，https 下载会报 TLS 初始化失败，补丁下载请用 http 或改用 64 位版。
   The 32-bit build ships without OpenSSL 1.1, so https downloads report a TLS init failure —
   use http, or download patches with the 64-bit build.
2. 高通 9008 / 苹果的驱动因授权没打包，装法看 补丁（Patch）\驱动说明（Driver-Guide）.txt。
   Qualcomm 9008 / Apple drivers are not bundled (license); see the driver guide in the patch folder.
3. 刷错分区或选错固件会变砖，第一次用请先只刷 boot 验证流程。
   A wrong partition or firmware bricks the device; flash only boot on the first run.
