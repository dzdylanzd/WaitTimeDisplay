@echo off
set SIM_DIR=%~dp0
set SIM_DIR=%SIM_DIR:~0,-1%
set BUILD_DIR=%SIM_DIR%\build
if exist "%BUILD_DIR%" (
    echo Removing %BUILD_DIR%...
    rmdir /s /q "%BUILD_DIR%"
    echo Done.
) else (
    echo Nothing to clean.
)
