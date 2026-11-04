@echo off
setlocal
chcp 65001 >nul
pushd "%~dp0"
if errorlevel 1 (
  echo Cannot open the game folder.
  pause
  exit /b 1
)
where gcc >nul 2>nul
if errorlevel 1 (
  echo GCC not found. Install MSYS2 UCRT64 GCC or download burger-windows.zip from Releases.
  popd
  pause
  exit /b 1
)
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude src/main.c src/game.c src/ranking.c src/terminal.c src/art.c src/ui.c -o burger.exe -lm
if errorlevel 1 (
  echo Build failed.
  popd
  pause
  exit /b 1
)
echo Build successful: burger.exe
popd
pause
exit /b 0
