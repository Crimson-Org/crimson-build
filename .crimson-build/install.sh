#!/bin/sh
# Generated from tasks.json by Crimson Build, edit that file instead
# Install the crimson-build command for the current user
set -eu
script_directory=$(cd "$(dirname "$0")" && pwd)
cd "$script_directory/.."

sh "$script_directory/build.sh"
export PROGRAM="crimson-build"
export VERSION="0.1.0"
export PREFIX="${HOME}/.local"
export STD="gnu11"
export WARN="-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter"
export OPT="-O2 -g"
export DEFINES="-DPROGRAM_VERSION=\"${VERSION}\""
export PKGS="glib-2.0 >= 2.74"
printf '==> %s\n' 'install -Dm755 build/${PROGRAM} ${PREFIX}/bin/${PROGRAM}'
install -Dm755 build/${PROGRAM} ${PREFIX}/bin/${PROGRAM}
