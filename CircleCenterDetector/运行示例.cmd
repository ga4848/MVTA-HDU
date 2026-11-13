@echo off
cd /d "%~dp0"
if not exist "bin\Release\CircleCenterDetector.exe" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
  if errorlevel 1 (pause & exit /b 1)
)
"bin\Release\CircleCenterDetector.exe" "images\sample.jpg" "output" --show
pause
