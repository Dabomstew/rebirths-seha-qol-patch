@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x64
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\asset-build mkdir build\asset-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\src\asset_store.cpp native\tests\asset_store_probe.cpp /Fo:build\asset-build\ /Fe:build\asset-build\asset-store-probe.exe /link bcrypt.lib
exit /b %errorlevel%
