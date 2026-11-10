@echo off
rem Builds build\cross-base-storage.dll and the build\dinput8.dll loader with the Visual Studio x64 compiler
rem (Visual Studio 2022 or newer with "Desktop development with C++").
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio not found.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0src"
if not exist ..\build mkdir ..\build
rc /nologo /fo cross_base_storage.res cross_base_storage.rc || exit /b 1
cl /nologo /O2 /W4 /LD cross_base_storage.c cross_base_storage.res /Fe:..\build\cross-base-storage.dll /Fo:cross_base_storage.obj /link /NOLOGO || exit /b 1
rc /nologo /fo dinput8_proxy.res dinput8_proxy.rc || exit /b 1
cl /nologo /O2 /W4 /LD dinput8_proxy.c dinput8_proxy.res /Fe:..\build\dinput8.dll /Fo:dinput8_proxy.obj /link /NOLOGO /EXPORT:DirectInput8Create || exit /b 1
del *.obj *.res ..\build\*.lib ..\build\*.exp 2>nul
echo Built build\cross-base-storage.dll and build\dinput8.dll
