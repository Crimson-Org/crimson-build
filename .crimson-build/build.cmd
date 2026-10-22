@echo off
rem Generated from tasks.json by Crimson Build, edit that file instead
rem Build the crimson-build command
setlocal
cd /d "%~dp0.."

set "PROGRAM=crimson-build"
set "VERSION=0.1.0"
set "PREFIX=%HOME%/.local"
set "STD=gnu11"
set "WARN=-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter"
set "PKGS=glib-2.0 gio-2.0"
echo(==^> needs cc
call :crimson_needs "cc"
if errorlevel 1 exit /b %errorlevel%
echo(==^> needs make
call :crimson_needs "make"
if errorlevel 1 exit /b %errorlevel%
echo(==^> needs pkg-config
call :crimson_needs "pkg-config"
if errorlevel 1 exit /b %errorlevel%
echo(==^> pkg glib-2.0 ^>= 2.74
call :crimson_pkg "glib-2.0 >= 2.74"
if errorlevel 1 exit /b %errorlevel%
echo(==^> pkg gio-2.0
call :crimson_pkg "gio-2.0"
if errorlevel 1 exit /b %errorlevel%
echo(==^> each src/*.c src/build/*.c
call :crimson_each
if errorlevel 1 exit /b %errorlevel%
echo(==^> cc -o build/%PROGRAM% build/obj/*.o $^(pkg-config --libs $PKGS^) -lm
cc -o build/%PROGRAM% build/obj/*.o $(pkg-config --libs $PKGS) -lm
if errorlevel 1 exit /b %errorlevel%
exit /b 0

:crimson_needs
where "%~1" >nul 2>&1 || (echo %~1 is needed, install it first & exit /b 1)
exit /b 0

:crimson_pkg
pkg-config --exists "%~1" || (echo The development files for %~1 are missing & exit /b 1)
exit /b 0

:crimson_each
echo Building each file needs a Unix shell with make
exit /b 1
