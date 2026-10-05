@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\adv-operations-test mkdir build\adv-operations-test
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\adv_operations_test.cpp /Fo:build\adv-operations-test\ /Fe:build\adv-operations-test\adv-operations-test.exe
if errorlevel 1 exit /b 1
build\adv-operations-test\adv-operations-test.exe
exit /b %errorlevel%
