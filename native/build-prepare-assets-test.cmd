@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x64
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\prepare mkdir build\prepare
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /utf-8 /O2 /W4 /EHsc /MT /DUNICODE /D_UNICODE /I native\include @native\prepare-assets-sources.rsp native\tools\prepare_assets_test_main.cpp /Fo:build\prepare\ /Fe:build\prepare\prepare-assets-test.exe /link bcrypt.lib
exit /b %errorlevel%
