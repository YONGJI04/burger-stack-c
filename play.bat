@echo off
setlocal
chcp 65001 >nul
pushd "%~dp0"
if errorlevel 1 (
  echo Cannot open the game folder.
  pause
  exit /b 1
)
if not exist burger.exe (
  echo burger.exe not found. Run build.bat first or download the Windows release.
  popd
  pause
  exit /b 1
)
burger.exe
set "burger_exit=%errorlevel%"
popd
if not "%burger_exit%"=="0" pause
exit /b %burger_exit%
