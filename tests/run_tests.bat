@echo off
set TESTS_DIR=%~dp0
set TESTS_DIR=%TESTS_DIR:~0,-1%
set EXE=%TESTS_DIR%\build\queuewatch_tests.exe
if not exist "%EXE%" (
    echo Not built yet.  Run tests\build_tests.bat first.
    exit /b 1
)
"%EXE%" --reporters=console --success
