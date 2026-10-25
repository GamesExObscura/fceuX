@echo off
setlocal
cd /d "%~dp0"
set "FCEUX_MENU_TRACE=%~dp0fullscreen-menu-trace.txt"
echo Click File and Options in windowed mode, then try them in fullscreen.
echo Then close FCEUX normally. This trace does not change your settings.
start "" /wait "%~dp0fceux.exe"
echo Trace saved to "%FCEUX_MENU_TRACE%"
pause
endlocal
