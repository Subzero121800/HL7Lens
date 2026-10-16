@echo off
rem HL7Lens installer: runs install.ps1 for this one time without changing the PowerShell execution policy.
rem Options are passed through, e.g.  install.cmd -NppPath D:\Tools\npp   or   install.cmd -Uninstall
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
pause
