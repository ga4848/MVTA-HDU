@echo off
cd /d "%~dp0"
if not exist "bin\Release\PaperclipCounter.exe" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
  if errorlevel 1 (pause & exit /b 1)
)
"bin\Release\PaperclipCounter.exe" "images\sample.png" "output" --show
pause
