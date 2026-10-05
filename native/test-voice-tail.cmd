@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\voice-tail-build mkdir build\voice-tail-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /DREBIRTHS_TEST_CONTRACTS /I native\include native\tools\voice_tail_test.cpp native\src\voice_tail.cpp native\src\rebirths_identity.cpp native\src\call_patch.cpp /Fo:build\voice-tail-build\ /Fe:build\voice-tail-build\voice-tail-test.exe /link bcrypt.lib
if errorlevel 1 exit /b 1
build\voice-tail-build\voice-tail-test.exe
exit /b %errorlevel%
