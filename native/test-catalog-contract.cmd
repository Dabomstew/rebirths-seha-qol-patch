@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native-build mkdir build\native-build
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\src\rebirths_identity.cpp native\tools\catalog_contract_test.cpp /Fo:build\native-build\ /link bcrypt.lib /OUT:build\native-build\catalog-contract-test.exe
if errorlevel 1 exit /b 1
build\native-build\catalog-contract-test.exe
exit /b %errorlevel%
