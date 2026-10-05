@echo off
setlocal
cd /d "%~dp0.."
call native\build-rebirths-proxy.cmd
if errorlevel 1 exit /b 1
call "%~dp0..\scripts\init-msvc.cmd" x86 >nul
if errorlevel 1 exit /b 1
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT native\tools\rebirths_proxy_smoke.cpp /Fo:build\native-build\ /Fe:build\native-build\rebirths-proxy-smoke.exe
if errorlevel 1 exit /b 1
build\native-build\rebirths-proxy-smoke.exe build\native-build\X3DAudio1_7.dll
if errorlevel 1 exit /b 1
python native\tests\rebirths_proxy_build_test.py build\native-build\X3DAudio1_7.dll
exit /b %errorlevel%
