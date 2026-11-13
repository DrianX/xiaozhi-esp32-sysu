@echo off
setlocal
cd /d "%~dp0"
title MPU-6050 Telemetry
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0server.ps1"
echo.
pause
