@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package-vortex.ps1" -LocalPreview
pause
