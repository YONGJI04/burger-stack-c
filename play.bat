@echo off
chcp 65001 >nul
cd /d "%~dp0"
if not exist burger.exe (
  echo burger.exe not found. Run build.bat first or download the Windows release.
  pause
  exit /b 1
)
burger.exe
if errorlevel 1 pause
