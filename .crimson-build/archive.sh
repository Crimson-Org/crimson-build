#!/bin/sh
# Generated from tasks.json by Crimson Build, edit that file instead
# Pack the crimson-build command into a tar.gz archive in dist
set -eu
script_directory=$(cd "$(dirname "$0")" && pwd)
cd "$script_directory/.."

sh "$script_directory/build.sh"
export PROGRAM="crimson-build"
export VERSION="0.1.0"
export PREFIX="${HOME}/.local"
export STD="gnu11"
export WARN="-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter"
export PKGS="glib-2.0 gio-2.0"
printf '==> %s\n' 'name=${PROGRAM}-${VERSION}-linux-$(uname -m) && rm -rf dist/stage && mkdir -p dist/stage/$name && cp build/${PROGRAM} LICENSE README.md dist/stage/$name/ && tar -C dist/stage -czf dist/$name.tar.gz $name && rm -rf dist/stage && echo Wrote dist/$name.tar.gz'
name=${PROGRAM}-${VERSION}-linux-$(uname -m) && rm -rf dist/stage && mkdir -p dist/stage/$name && cp build/${PROGRAM} LICENSE README.md dist/stage/$name/ && tar -C dist/stage -czf dist/$name.tar.gz $name && rm -rf dist/stage && echo Wrote dist/$name.tar.gz
