@echo off
set TESTS_DIR=%~dp0
set TESTS_DIR=%TESTS_DIR:~0,-1%
set BUILD_DIR=%TESTS_DIR%\build
if exist "%BUILD_DIR%" (
    echo Removing %BUILD_DIR%...
    rmdir /s /q "%BUILD_DIR%"
    echo Done.
) else (
    echo Nothing to clean.
)
