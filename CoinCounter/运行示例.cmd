@echo off
cd /d "%~dp0"
if not exist "bin\Release\CoinCounter.exe" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
  if errorlevel 1 (pause & exit /b 1)
)
"bin\Release\CoinCounter.exe" "images\sample.jpg" "output" --show
pause
