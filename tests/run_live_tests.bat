@echo off
rem Build and run the LIVE integration tests (needs internet).
rem Checks that fetchRideData can parse every park on queue-times.com.
cd /d "%~dp0"

call build_tests.bat
if errorlevel 1 exit /b 1

echo.
echo Running live API tests (this fetches every park - takes a few minutes)...
build\queuewatch_live_tests.exe --no-intro
exit /b %errorlevel%
