@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\battle-delay-test mkdir build\battle-delay-test
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\rebirth3_battle_delay_test.cpp /Fo:build\battle-delay-test\ /Fe:build\battle-delay-test\battle-delay.exe
if errorlevel 1 exit /b 1
build\battle-delay-test\battle-delay.exe
exit /b %errorlevel%
