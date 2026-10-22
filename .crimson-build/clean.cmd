@echo off
rem Generated from tasks.json by Crimson Build, edit that file instead
rem Remove the build folder
setlocal
cd /d "%~dp0.."

set "PROGRAM=crimson-build"
set "VERSION=0.1.0"
set "PREFIX=%HOME%/.local"
set "STD=gnu11"
set "WARN=-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter"
set "PKGS=glib-2.0 gio-2.0"
echo(==^> remove build
call :crimson_remove "build"
if errorlevel 1 exit /b %errorlevel%
exit /b 0

:crimson_remove
if exist "%~1\*" (
	rmdir /s /q "%~1"
	exit /b
)
if exist "%~1" del /f /q "%~1"
exit /b 0
