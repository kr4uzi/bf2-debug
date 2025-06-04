@echo off
setlocal enabledelayedexpansion
goto :launchvscode

REM 2024/05/20 mkr: remove bf2 workspace folder are added by the vscode plugin
REM check for battlefield 2 in the current directory, then in bf2.props
if not exist ".\bf2_w32ded.exe" (
  for /f "tokens=*" %%i in ('findstr /c:"<BF2_DIR>" bf2.props') do set "bf2dir=%%i"
  set "bf2dir=!bf2dir:~9,-10!"
)

REM check for battlefield 2 server installation (registry)
if not exist "%bf2dir%\bf2_w32ded.exe" (
  for /f "tokens=2* skip=2" %%a in ('reg query "HKLM\SOFTWARE\EA GAMES\Battlefield 2 Server" /v "GAMEDIR" /reg:32') do set "bf2dir=%%b"
)

REM check for battlefield 2 client (also contains the server) installation (registry)
if not exist "%bf2dir%\bf2_w32ded.exe" (
  for /f "tokens=2* skip=2" %%a in ('reg query "HKLM\SOFTWARE\Electronic Arts\EA Games\Battlefield 2" /v "InstallDir" /reg:32') do set "bf2dir=%%b"
)

if not exist "%bf2dir%\bf2_w32ded.exe" (
  echo Battlefield 2 directory not found in this folder, in bf2.props or in registry.
  pause
  exit /b 1
)

:launchvscode
REM check if VS Code installation is in PATH
if exist "%temp%\bf2py-data" (
  rmdir /s /q "%temp%\bf2py-data"
)

mkdir "%temp%\bf2py-data"

where code >nul 2>nul
if %errorlevel% == 0 (
  echo Waiting for VS Code to quit ...
  code --disable-workspace-trust --user-data-dir="%temp%\bf2py-data" --extensions-dir="%temp%\bf2py-data" --extensionDevelopmentPath="%~dp0vscode-ext" "vscode-ext\workspace"
  goto :exit
)

for %%d in (
    "%LocalAppData%\Programs\Microsoft VS Code\Code.exe"
    "%ProgramFiles%\Microsoft VS Code\Code.exe"
    "%ProgramFiles(x86)%\Microsoft VS Code\Code.exe"
) do (
    if not defined vscodePath if exist "%%d" set "vscodePath=%%d"
)

if not defined vscodePath (
  echo Unable to detect VS Code installation.
  echo Please open the vscode-ext folder using Visual Studio Code manually and run the extension.
  pause
  goto :exit
)

"%vscodePath%" --disable-workspace-trust --user-data-dir="%temp%\bf2py-data" --extensions-dir="%temp%\bf2py-data" --extensionDevelopmentPath=%~dp0vscode-ext "vscode-ext/workspace"
:exit
if exist "%temp%\bf2py-data" (
  rmdir /s /q "%temp%\bf2py-data"
)
endlocal
exit /b 0