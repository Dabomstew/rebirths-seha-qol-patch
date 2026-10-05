@echo off
setlocal
call "%~dp0build-preparer-proxy.cmd"
if errorlevel 1 exit /b 1
call "%~dp0..\scripts\init-msvc.cmd" x64
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\prepare mkdir build\prepare
rc /nologo /fo build\prepare\prepare_game.res native\tools\prepare_game.rc
if errorlevel 1 exit /b 1
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /utf-8 /O2 /W4 /EHsc /MT /DUNICODE /D_UNICODE /DREBIRTHS_PREPARE_TEST /I native\include @native\prepare-assets-sources.rsp @native\prepare-install-sources.rsp native\tools\prepare_game_test_main.cpp build\prepare\prepare_game.res /Fo:build\prepare\ /Fe:build\prepare\prepare-game-test.exe /link bcrypt.lib imagehlp.lib advapi32.lib
exit /b %errorlevel%
