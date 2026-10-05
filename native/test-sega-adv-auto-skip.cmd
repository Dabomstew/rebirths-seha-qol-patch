@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /I. /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /DREBIRTHS_TEST_CONTRACTS /I native\include /I native\tools native\src\rebirths_identity.cpp native\src\call_patch.cpp native\src\sega_adv_auto_skip.cpp native\tools\sega_adv_auto_skip_test.cpp /Fo:build\native-build\ /link bcrypt.lib /OUT:build\native-build\sega_adv_auto_skip_test.exe
if errorlevel 1 exit /b 1
build\native-build\sega_adv_auto_skip_test.exe
exit /b %errorlevel%
