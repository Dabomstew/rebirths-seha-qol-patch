@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\audio-tail-build mkdir build\audio-tail-build
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\audio_tail_test.cpp /Fo:build\audio-tail-build\ /Fe:build\audio-tail-build\audio-tail-test.exe /link bcrypt.lib
if errorlevel 1 exit /b 1
build\audio-tail-build\audio-tail-test.exe
exit /b %errorlevel%
