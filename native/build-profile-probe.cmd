@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PROFILE_VS=%%i"
if not defined PROFILE_VS exit /b 1
call "%PROFILE_VS%\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
set "PROFILE_INCLUDE=%~1"
if not defined PROFILE_INCLUDE set "PROFILE_INCLUDE=native\include"
set "PROFILE_OUT=%~2"
if not defined PROFILE_OUT set "PROFILE_OUT=build\profile-probe"
if not exist "%PROFILE_OUT%" mkdir "%PROFILE_OUT%"
cl /nologo /std:c++17 /O2 /W4 /EHsc /MT /I "%PROFILE_INCLUDE%" native\tools\profile_probe.cpp /Fo:"%PROFILE_OUT%\profile-probe.obj" /Fe:"%PROFILE_OUT%\profile-probe.exe"
exit /b %errorlevel%
