@echo off
title Skorbord eklentisini kaldir
net session >nul 2>&1
if errorlevel 1 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
rmdir /S /Q "%ProgramData%\obs-studio\plugins\obs-skorbord"
echo Skorbord eklentisi kaldirildi.
pause
