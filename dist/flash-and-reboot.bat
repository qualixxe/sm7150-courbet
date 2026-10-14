@echo off
REM Auto-flash boot + reboot. Usage: flash-and-reboot.bat [image]
setlocal
cd /d "%~dp0"
set "IMG=%~1"
if "%IMG%"=="" set "IMG=C:\Users\qualixxe\Documents\Default Project\work\sm7150-sweet\dist\courbet-with-courbet-dtb.img"
if not exist "%IMG%" (
  echo ERROR: no such image: %IMG%
  exit /b 1
)
set "FB=C:\Users\qualixxe\Desktop\courbet-uefi\fastboot.exe"
if not exist "%FB%" (
  echo ERROR: fastboot.exe not found: %FB%
  exit /b 1
)
echo.
echo =================================================
echo  Flashing boot : %IMG%
echo =================================================
echo.
"%FB%" flash boot "%IMG%"
if errorlevel 1 (
  echo.
  echo Flash failed. Phone in fastboot? Hold Power + Volume Down.
  exit /b 1
)
echo.
echo Written. Rebooting...
"%FB%" reboot
echo Done.
endlocal
