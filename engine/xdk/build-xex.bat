@echo off
rem Unattended Release build of default.xex. Run from an "Xbox 360 Command
rem Prompt" or any prompt where %XEDK% is set and msbuild is on PATH.
rem Usage: build-xex.bat [Release|Debug]
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
cd /d "%~dp0"

if "%XEDK%"=="" (
  echo XEDK is not set: the Xbox 360 XDK is not installed or this is not the XDK prompt.
  exit /b 2
)
where msbuild >nul 2>nul || (
  echo msbuild not on PATH. Run from "Visual Studio Command Prompt (2010)" or add
  echo %%WINDIR%%\Microsoft.NET\Framework\v4.0.30319 to PATH.
  exit /b 2
)

msbuild vsdemake.sln /t:Rebuild /p:Configuration=%CONFIG% /p:Platform="Xbox 360" /nologo /m ^
  /fl /flp:logfile=build-%CONFIG%.log;verbosity=normal
set RC=%ERRORLEVEL%

if not "%RC%"=="0" (
  echo.
  echo BUILD FAILED ^(%RC%^). First errors:
  findstr /i /c:"error " build-%CONFIG%.log
  exit /b %RC%
)
if not exist out\%CONFIG%\default.xex (
  echo msbuild succeeded but out\%CONFIG%\default.xex is missing.
  echo Check Configuration Properties ^> Xbox 360 Image Conversion, or run:
  echo   imagexex /out:out\%CONFIG%\default.xex out\%CONFIG%\default.exe
  exit /b 3
)
echo.
echo OK: out\%CONFIG%\default.xex
for %%F in (out\%CONFIG%\default.xex) do echo size: %%~zF bytes
endlocal
