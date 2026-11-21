@echo off
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Please run this file as administrator.
    echo         请右键 - 以管理员身份运行。
    echo.
    pause
    exit /b 1
)

if not exist "drivers\google-usb\android_winusb.inf" (
    echo [ERROR] drivers\google-usb\android_winusb.inf not found.
    pause
    exit /b 1
)

echo Installing Google USB Driver (ADB + Fastboot) ...
pnputil /add-driver "drivers\google-usb\android_winusb.inf" /install
echo.
echo Driver package added. Replug your phone, then check Device Manager:
echo   Android ADB Interface  /  Android Bootloader Interface
echo.
pause
