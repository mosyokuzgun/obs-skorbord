@echo off
title Skorbord eklentisi kurulumu
net session >nul 2>&1
if errorlevel 1 (
  echo Yonetici izni isteniyor...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
tasklist /FI "IMAGENAME eq obs64.exe" 2>nul | find /I "obs64.exe" >nul
if not errorlevel 1 (
  echo.
  echo OBS acik. Lutfen once OBS'i kapatin, sonra bir tusa basin.
  pause >nul
)
set "DEST=%ProgramData%\obs-studio\plugins"
if not exist "%DEST%" mkdir "%DEST%"
xcopy /E /I /Y /Q "%~dp0obs-skorbord" "%DEST%\obs-skorbord" >nul
if errorlevel 1 (
  echo.
  echo HATA: Dosyalar kopyalanamadi.
  pause
  exit /b 1
)
echo.
echo Skorbord eklentisi kuruldu:
echo   %DEST%\obs-skorbord
echo.
echo OBS'i acin. Skorbord paneli sag tarafta kendiliginden acilacak.
echo Sonra Kaynaklar + menusunden "Skorbord" kaynagini ekleyin.
echo.
pause
