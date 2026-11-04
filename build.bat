@echo off
chcp 65001 >nul
where gcc >nul 2>nul
if errorlevel 1 (
  echo GCC not found. Install MSYS2 UCRT64 GCC or download burger-windows.zip from Releases.
  pause
  exit /b 1
)
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude src/main.c src/game.c src/ranking.c src/terminal.c src/art.c src/ui.c -o burger.exe -lm
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
echo Build successful: burger.exe
pause
