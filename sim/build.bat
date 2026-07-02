@echo off
setlocal
set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set VSDEV=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat
set SIM_DIR=%~dp0
set SIM_DIR=%SIM_DIR:~0,-1%
set BUILD_DIR=%SIM_DIR%\build

call "%VSDEV%" >nul 2>&1

if not exist "%BUILD_DIR%" (
    echo Configuring...
    "%CMAKE%" -S "%SIM_DIR%" -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 ( echo Configure failed & exit /b 1 )
)

echo Building...
"%CMAKE%" --build "%BUILD_DIR%"
if errorlevel 1 ( echo Build failed & exit /b 1 )

copy /y "C:\SDL2\lib\x64\SDL2.dll" "%BUILD_DIR%\SDL2.dll" >nul 2>&1
echo Done.  Run sim\run.bat to launch.
