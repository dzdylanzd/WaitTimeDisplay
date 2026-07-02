@echo off
setlocal
set ROOT=%~dp0
set ROOT=%ROOT:~0,-1%
set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set VSDEV=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat
set TESTS_DIR=%ROOT%\tests
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

echo.
echo Running tests...
"%BUILD_DIR%\queuewatch_tests.exe" --reporters=console --success
