@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\owned-patch-install-test mkdir build\owned-patch-install-test
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\owned_patch_install_test.cpp /Fo:build\owned-patch-install-test\ /Fe:build\owned-patch-install-test\owned_sequence_contract_test.exe
if errorlevel 1 exit /b 1
build\owned-patch-install-test\owned_sequence_contract_test.exe
exit /b %errorlevel%
