@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\save-anywhere-test mkdir build\save-anywhere-test
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\save_anywhere_test.cpp /Fo:build\save-anywhere-test\ /Fe:build\save-anywhere-test\save-anywhere.exe
if errorlevel 1 exit /b 1
build\save-anywhere-test\save-anywhere.exe
exit /b %errorlevel%
