@echo off
setlocal
rem Compatibility entry point; the proxy supports all four games.
call "%~dp0build-rebirths-proxy.cmd"
exit /b %errorlevel%
