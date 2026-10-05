@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-proxy.ps1" -Variant normal
exit /b %errorlevel%
