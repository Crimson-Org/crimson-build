@echo off
rem Generated from tasks.json by Crimson Build, edit that file instead
rem Pack the crimson-build command into a tar.gz archive in dist
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
echo(==^> name=%PROGRAM%-%VERSION%-linux-$^(uname -m^) ^&^& rm -rf dist/stage ^&^& mkdir -p dist/stage/$name ^&^& cp build/%PROGRAM% LICENSE README.md dist/stage/$name/ ^&^& tar -C dist/stage -czf dist/$name.tar.gz $name ^&^& rm -rf dist/stage ^&^& echo Wrote dist/$name.tar.gz
name=%PROGRAM%-%VERSION%-linux-$(uname -m) && rm -rf dist/stage && mkdir -p dist/stage/$name && cp build/%PROGRAM% LICENSE README.md dist/stage/$name/ && tar -C dist/stage -czf dist/$name.tar.gz $name && rm -rf dist/stage && echo Wrote dist/$name.tar.gz
if errorlevel 1 exit /b %errorlevel%
exit /b 0
