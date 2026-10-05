@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\tests mkdir build\tests
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\tutorial_skip_test.cpp native\src\rebirths_identity.cpp bcrypt.lib /Fo:build\tests\ /Fe:build\tests\tutorial-skip.exe
if errorlevel 1 exit /b 1
if exist ..\build\rebirth1\tutorial-script-0214.json (
    ..\.runtime-venv\Scripts\python.exe native\tests\tutorial_script_fixture.py
    if errorlevel 1 exit /b 1
    build\tests\tutorial-skip.exe build\tests\tutorial-script-rebirth1.bin build\tests\tutorial-script-rebirth2.bin build\tests\tutorial-script-rebirth3.bin build\tests\tutorial-script-sega-hard-girls.bin
    if errorlevel 1 exit /b 1
    exit /b 0
)
build\tests\tutorial-skip.exe
exit /b %errorlevel%
