#!/bin/sh
# Generated from tasks.json by Crimson Build, edit that file instead
# Remove the build folder
set -eu
script_directory=$(cd "$(dirname "$0")" && pwd)
cd "$script_directory/.."

crimson_remove() {
	rm -rf "$1"
}

export PROGRAM="crimson-build"
export VERSION="0.1.0"
export PREFIX="${HOME}/.local"
export STD="gnu11"
export WARN="-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter"
export OPT="-O2 -g"
export DEFINES="-DPROGRAM_VERSION=\"${VERSION}\""
export PKGS="glib-2.0 >= 2.74"
printf '==> %s\n' 'remove build'
crimson_remove "build"
