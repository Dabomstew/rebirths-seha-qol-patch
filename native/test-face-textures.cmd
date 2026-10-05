@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\tests mkdir build\tests
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /I native\include native\tools\face_texture_creation_test.cpp native\src\rebirths_identity.cpp bcrypt.lib /Fo:build\tests\ /Fe:build\tests\face-textures.exe
if errorlevel 1 exit /b 1
build\tests\face-textures.exe
exit /b %errorlevel%
