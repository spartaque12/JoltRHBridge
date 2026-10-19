@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (
    echo Visual Studio not found.
    exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /O2 /GS- /W3 /LD /MT /D_CRT_SECURE_NO_WARNINGS JoltRHBridge.c /link /OUT:JoltRHBridge.asi /DLL kernel32.lib
