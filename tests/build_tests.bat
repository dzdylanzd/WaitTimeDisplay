@echo off
setlocal
set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set VSDEV=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat
set TESTS_DIR=%~dp0
set TESTS_DIR=%TESTS_DIR:~0,-1%
set BUILD_DIR=%TESTS_DIR%\build

call "%VSDEV%" >nul 2>&1

if not exist "%BUILD_DIR%" (
    echo Configuring tests...
    "%CMAKE%" -S "%TESTS_DIR%" -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 ( echo Configure failed & exit /b 1 )
)

echo Building tests...
"%CMAKE%" --build "%BUILD_DIR%"
if errorlevel 1 ( echo Build failed & exit /b 1 )
echo Done.  Run tests\run_tests.bat to execute.
