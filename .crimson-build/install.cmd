@echo off
rem Generated from tasks.json by Crimson Build, edit that file instead
rem Install the crimson-build command for the current user
setlocal
cd /d "%~dp0.."

call "%~dp0build.cmd"
if errorlevel 1 exit /b %errorlevel%
set "PROGRAM=crimson-build"
set "VERSION=0.1.0"
set "PREFIX=%HOME%/.local"
set "STD=gnu11"
set "WARN=-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter"
set "PKGS=glib-2.0 gio-2.0"
echo(==^> install -Dm755 build/%PROGRAM% %PREFIX%/bin/%PROGRAM%
install -Dm755 build/%PROGRAM% %PREFIX%/bin/%PROGRAM%
if errorlevel 1 exit /b %errorlevel%
exit /b 0
