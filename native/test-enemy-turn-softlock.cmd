@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\enemy-turn-test mkdir build\enemy-turn-test
cl /nologo /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\enemy_turn_softlock_test.cpp /Fo:build\enemy-turn-test\ /Fe:build\enemy-turn-test\enemy-turn.exe
if errorlevel 1 exit /b 1
build\enemy-turn-test\enemy-turn.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\enemy_turn_worker_test.cpp /Fo:build\enemy-turn-test\ /Fe:build\enemy-turn-test\worker.exe
if errorlevel 1 exit /b 1
build\enemy-turn-test\worker.exe
exit /b %errorlevel%
