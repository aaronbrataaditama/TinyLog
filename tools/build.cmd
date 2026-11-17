@echo off
rem Usage: tools\build.cmd [debug|release]   (from Bash: cmd //c tools\build.cmd debug)
setlocal
set PRESET=%~1
if "%PRESET%"=="" set PRESET=debug
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR set VSDIR=C:\Program Files\Microsoft Visual Studio\18\Enterprise
if not defined VCToolsInstallDir call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
ctest --preset %PRESET% || exit /b 1
echo BUILD+TEST OK (%PRESET%)
