@echo off
setlocal
rem Compatibility entry point; validates the shared four-game proxy.
call "%~dp0test-rebirths-proxy.cmd"
exit /b %errorlevel%
