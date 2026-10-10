@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Prepare-Sandbox.ps1"
if errorlevel 1 exit /b 1
echo Abra ClickLimit-Benchmark.wsb para iniciar o teste no Windows Sandbox.
pause
