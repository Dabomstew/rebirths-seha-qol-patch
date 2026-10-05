@echo off
setlocal
call :test 32 x86
if errorlevel 1 exit /b 1
call :test 64 x64
exit /b %errorlevel%

:test
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars%1.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\platform-util-build\%2 mkdir build\platform-util-build\%2
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\platform_util_test.cpp /Fo:build\platform-util-build\%2\ /Fe:build\platform-util-build\%2\platform-util-test.exe /link bcrypt.lib
if errorlevel 1 exit /b 1
build\platform-util-build\%2\platform-util-test.exe
exit /b %errorlevel%
