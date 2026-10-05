@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\src\rebirth1_adv_visual_speed.cpp native\tools\rebirth1_adv_visual_speed_test.cpp /Fo:build\native-build\ /Fe:build\native-build\rebirth1-adv-visual-speed-test.exe
if errorlevel 1 exit /b 1
build\native-build\rebirth1-adv-visual-speed-test.exe
exit /b %errorlevel%
