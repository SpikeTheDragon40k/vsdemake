@echo off
rem Unattended Release build of default.xex. Run from an "Xbox 360 Command
rem Prompt" or any prompt where %XEDK% is set and msbuild is on PATH.
rem Usage: build-xex.bat [Release|Debug]
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
cd /d "%~dp0"

rem Copying through an ISO or an old FAT tool shortens names to 8.3
rem (vs_platform_xdk.cpp becomes VS_PLATF.CPP) and nothing compiles.
if not exist ..\src\core\vs_compat.h goto badcopy
if not exist ..\src\platform\xdk\vs_platform_xdk.cpp goto badcopy

if "%XEDK%"=="" (
  echo XEDK is not set: the Xbox 360 XDK is not installed or this is not the XDK prompt.
  exit /b 2
)
rem msbuild for VS2010 ships with .NET 4 at a fixed place; use it when the
rem prompt has no msbuild on PATH (a plain cmd window).
where msbuild >nul 2>nul && goto havemsbuild
if exist "%WINDIR%\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe" (
  set "PATH=%WINDIR%\Microsoft.NET\Framework\v4.0.30319;%PATH%"
  goto havemsbuild
)
if exist "%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe" (
  set "PATH=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319;%PATH%"
  goto havemsbuild
)
echo msbuild not found. Install Visual Studio 2010 ^(it installs .NET 4^), or open
echo Start ^> All Programs ^> Microsoft Visual Studio 2010 ^> Visual Studio Tools ^>
echo Visual Studio Command Prompt ^(2010^) and run this script from there.
exit /b 2
:havemsbuild
rem The Visual C++ environment ^(compiler paths^) for msbuild.
if exist "%VS100COMNTOOLS%vsvars32.bat" call "%VS100COMNTOOLS%vsvars32.bat" >nul

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
exit /b 0

:badcopy
echo The source files have shortened names ^(for example VS_PLATF.CPP instead of
echo vs_platform_xdk.cpp^). The copy into this machine truncated them.
echo Copy the repository again with a VirtualBox shared folder, a .zip file,
echo or git clone. See docs\XBOX360_GUIDE.md, "Building in a VirtualBox VM".
exit /b 4
