@echo off
set SIM_DIR=%~dp0
set SIM_DIR=%SIM_DIR:~0,-1%
set EXE=%SIM_DIR%\build\queuewatch_sim.exe
if not exist "%EXE%" (
    echo Not built yet.  Run sim\build.bat first.
    exit /b 1
)
start "" "%EXE%"
