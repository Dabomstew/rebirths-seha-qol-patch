@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\load-timing-test mkdir build\load-timing-test
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\load_timing_test.cpp /Fo:build\load-timing-test\ /Fe:build\load-timing-test\native.exe /link user32.lib
if errorlevel 1 exit /b 1
build\load-timing-test\native.exe
exit /b %errorlevel%
