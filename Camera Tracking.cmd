@echo off
setlocal
set "AWARENESS_CAMERA_SOURCE=%~dp0"
if exist "%~dp0ObserverDemo.exe" (
  start "" "%~dp0ObserverDemo.exe" --camera
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -Command ". (Join-Path $env:AWARENESS_CAMERA_SOURCE 'scripts\build-support.ps1'); & (Join-Path (Get-DesktopDirectory) 'CS2-Observer-Overlay\ObserverDemo.exe') --camera"
)
