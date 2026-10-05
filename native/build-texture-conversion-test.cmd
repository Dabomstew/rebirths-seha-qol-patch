@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\texture-test mkdir build\texture-test
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\texture_conversion_test.cpp /Fo:build\texture-test\ /Fe:build\texture-test\texture-test.exe
if errorlevel 1 exit /b 1
build\texture-test\texture-test.exe
exit /b %errorlevel%
