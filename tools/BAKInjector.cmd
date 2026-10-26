@echo off
set "INJECTOR=%~dp0..\bin\v5.46\Release\BAKInjector.exe"
if not exist "%INJECTOR%" (
  echo BAKInjector.exe is missing. Build or extract the BAKTweaks_5.46 package first.
  pause
  exit /b 1
)
start "" "%INJECTOR%"
