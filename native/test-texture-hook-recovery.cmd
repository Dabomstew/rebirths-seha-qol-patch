@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\texture-hook-test mkdir build\texture-hook-test
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\texture_hook_recovery_test.cpp native\src\rebirths_identity.cpp bcrypt.lib /Fo:build\texture-hook-test\ /Fe:build\texture-hook-test\texture-hooks.exe
if errorlevel 1 exit /b 1
build\texture-hook-test\texture-hooks.exe
exit /b %errorlevel%
