@echo off
setlocal
if exist "%~dp0VelaPokaReceiver.exe" (
  start "" "%~dp0VelaPokaReceiver.exe"
  exit /b 0
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start.ps1"
if errorlevel 1 pause
