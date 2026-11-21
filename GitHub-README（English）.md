qFlipper · Android Flasher Edition
=======================================================

[Repository "About" one-liner — paste straight into the GitHub description field]
A Windows desktop tool forked from the official qFlipper: every Flipper flashing feature kept, plus an Android tab that detects ADB / Fastboot / Qualcomm 9008 / iPhone devices, flashes images, burns rawprogram.xml firmware, reboots to system / bootloader / recovery / 9008, and switches the whole UI between Chinese and English.


[Intro]

This is a community fork of Flipper Devices' official desktop app qFlipper (GPL-3.0). All original features — connecting a Flipper, firmware updates, file browsing, remote control — are untouched and the original folder layout is preserved. On top of that there is a separate "Android" tab, so one tool can manage a Flipper and rescue a phone.

The Android panel scans USB every 1.2 seconds and classifies devices by VID/PID and interface protocol into four groups: normal Android (ADB), Fastboot, Qualcomm 9008 (QDL, shown with its COM port) and iPhone / iPad (DFU, recovery or normal). In Fastboot you can pick an image and flash a given partition, erase a partition, and reboot four ways: back to system, back to bootloader, into recovery, or straight into 9008 (it tries "reboot edl" first and falls back to "oem edl" when the device refuses). In 9008 mode it loads rawprogram.xml plus a programmer and writes each partition listed in the XML. Apple devices are detected and described only — but the CPID is parsed out of the DFU/recovery serial number, so the panel can tell you it is an A11 / A12 / A13 class chip.

To avoid bricking the wrong model, the panel ships a model profile table: once a device is connected it reads the fastboot "product" value and compares it with the chosen profile, refusing to flash or erase on a mismatch. The whole page has Chinese and English strings; a small round button flips the entire UI, and a script in the patch folder makes the choice permanent.

One installer does the job: it reads your Windows architecture and installs only the 32-bit or 64-bit build, producing the same flat folder layout as the official package.

Warning: the flashing flow has not been validated end-to-end on real hardware. A wrong partition or wrong firmware bricks the device — flash only boot first to verify the flow. iOS is detection-only.


[Suggested screenshots to add]
- Android panel with a Fastboot device detected
- 9008 burn in progress
- Chinese vs English UI side by side
