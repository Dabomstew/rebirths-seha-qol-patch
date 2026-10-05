@echo off
setlocal
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
call "%~dp0..\scripts\init-msvc.cmd" x86 >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /DREBIRTHS_TEST_CONTRACTS /I native\include /I native\tools native\src\rebirths_identity.cpp native\src\call_patch.cpp native\src\rebirth3_adv_auto_skip.cpp native\tools\rebirth3_adv_auto_skip_test.cpp /Fo:build\native-build\ /link bcrypt.lib /OUT:build\native-build\rebirth3_adv_auto_skip_test.exe
if errorlevel 1 exit /b %errorlevel%
build\native-build\rebirth3_adv_auto_skip_test.exe
