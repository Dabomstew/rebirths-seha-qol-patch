@echo off
setlocal
call :test 32 x86
if errorlevel 1 exit /b 1
call :test 64 x64
exit /b %errorlevel%

:test
call "%~dp0..\scripts\init-msvc.cmd" %2
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\preparer-session\%2 mkdir build\preparer-session\%2
cl /nologo /std:c++17 /utf-8 /O2 /W4 /WX /EHsc /MT /DUNICODE /D_UNICODE native\preparer-common\preparer.cpp native\preparer-common\session_test.cpp /Fo:build\preparer-session\%2\ /Fe:build\preparer-session\%2\session-test.exe
if errorlevel 1 exit /b 1
build\preparer-session\%2\session-test.exe
exit /b %errorlevel%
