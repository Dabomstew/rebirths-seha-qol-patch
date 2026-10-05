@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\call-test mkdir build\call-test
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /DREBIRTHS_TEST_CONTRACTS /I native\include native\src\call_patch.cpp native\tools\call_transaction_test.cpp /Fo:build\call-test\ /Fe:build\call-test\call-test.exe /link /SECTION:.txprobe,ER
if errorlevel 1 exit /b 1
build\call-test\call-test.exe
exit /b %errorlevel%
