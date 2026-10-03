@echo off
rem Double-click to open the NorthstarPS4 token helper window, which signs the
rem PS4 build in to Northstar through the EA app on this PC.
rem From a command prompt, options run it in the terminal instead, e.g.
rem   NorthstarPS4-TokenHelper.cmd -Console "192.168.1.20 4821"
rem   NorthstarPS4-TokenHelper.cmd -Console local -Once
rem See Start-AtlasTokenHelper.ps1 next to this file for every option.
if "%~1"=="" (
    start "" powershell -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0Start-AtlasTokenHelper.ps1" -Gui
    exit /b 0
)
title NorthstarPS4 token helper
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-AtlasTokenHelper.ps1" %*
if errorlevel 1 pause
