@echo off
set ROOT=%~dp0
set ROOT=%ROOT:~0,-1%
set EXE=%ROOT%\sim\build\queuewatch_sim.exe
if not exist "%EXE%" (
    echo Simulator not built yet.  Run build_sim.bat first.
    exit /b 1
)
start "" "%EXE%"
