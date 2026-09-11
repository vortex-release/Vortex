@echo off
if "%~1"=="" (echo Usage: "Publish Update.cmd" MAJOR.MINOR.PATCH & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0release.ps1" "%~1"
exit /b %errorlevel%
