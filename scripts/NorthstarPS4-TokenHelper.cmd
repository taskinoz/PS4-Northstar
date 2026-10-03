@echo off
rem Double-click to sign the PS4 build in to Northstar through the EA app.
rem See Start-AtlasTokenHelper.ps1 next to this file.
title NorthstarPS4 token helper
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-AtlasTokenHelper.ps1" %*
if errorlevel 1 pause
