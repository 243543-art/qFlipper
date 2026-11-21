@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem [ANDROID-FLASHER] 语言开关：便携版写 ..\english.flag；安装版分别写 app64 / app32
set FLAG_A=..\english.flag
set FLAG_B=..\app64\english.flag
set FLAG_C=..\app32\english.flag

set HAVE=
if exist "%FLAG_A%" set HAVE=1
if exist "%FLAG_B%" set HAVE=1
if exist "%FLAG_C%" set HAVE=1

if defined HAVE (
    del /q "%FLAG_A%" 2>nul
    del /q "%FLAG_B%" 2>nul
    del /q "%FLAG_C%" 2>nul
    echo [LANGUAGE] English OFF - 当前: 中文
) else (
    if exist "..\qFlipper.exe" copy /y nul "%FLAG_A%" >nul 2>nul
    if exist "..\app64" copy /y nul "%FLAG_B%" >nul 2>nul
    if exist "..\app32" copy /y nul "%FLAG_C%" >nul 2>nul
    echo [LANGUAGE] English ON - 当前: English
)

echo.
echo Restart qFlipper to apply. / 重新打开 qFlipper 生效。
echo.
pause
