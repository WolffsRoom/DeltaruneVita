@echo off
setlocal
cd /d "%~dp0.."
if not exist scratch mkdir scratch
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /TC tests\audio_stream_clock_test.c /Fescratch\audio_stream_clock_test.exe /Foscratch\
if errorlevel 1 exit /b 1
scratch\audio_stream_clock_test.exe data\prepared\deltarune\deltarunevita\music\tenna_battle.ogg
if errorlevel 1 exit /b 1
scratch\audio_stream_clock_test.exe data\prepared\deltarune\deltarunevita\music\tenna_battle_guitar.ogg
exit /b %ERRORLEVEL%
