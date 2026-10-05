@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\asset-entry-build mkdir build\asset-entry-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\src\asset_store.cpp native\src\asset_entry.cpp native\tests\asset_entry_test.cpp /Fo:build\asset-entry-build\ /Fe:build\asset-entry-build\asset-entry-test.exe /link bcrypt.lib
exit /b %errorlevel%
