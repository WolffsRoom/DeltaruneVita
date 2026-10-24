@echo off
cd /d "%~dp0"
if exist "build_log.txt" del "build_log.txt"
echo BUILD_START=%DATE% %TIME% > "build_log.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\build-butterscotch-probe.ps1" >> "build_log.txt" 2>&1
echo EXIT_CODE=%ERRORLEVEL% >> "build_log.txt"
echo BUILD_END=%DATE% %TIME% >> "build_log.txt"
