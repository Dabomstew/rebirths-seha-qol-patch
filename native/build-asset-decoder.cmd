@echo off
setlocal
call "%~dp0..\scripts\init-msvc.cmd" x64
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\asset-build mkdir build\asset-build
cl /nologo /FIbuild/generated/release-version.hpp /std:c++17 /O2 /W4 /EHsc /MT /LD native\src\asset_huffman.cpp /Fo:build\asset-build\asset_huffman.obj /link /OUT:build\asset-build\asset-decoder.dll /IMPLIB:build\asset-build\asset-decoder.lib
exit /b %errorlevel%
