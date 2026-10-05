@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\thread-freeze-test mkdir build\thread-freeze-test
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\thread_freeze_test.cpp /Fo:build\thread-freeze-test\ /Fe:build\thread-freeze-test\thread-freeze-test.exe
if errorlevel 1 exit /b 1
build\thread-freeze-test\thread-freeze-test.exe
exit /b %errorlevel%
