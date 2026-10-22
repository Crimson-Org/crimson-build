#!/bin/sh
# Generated from tasks.json by Crimson Build, edit that file instead
# Build the crimson-build command
set -eu
script_directory=$(cd "$(dirname "$0")" && pwd)
cd "$script_directory/.."

crimson_needs() {
	command -v "$1" >/dev/null 2>&1 || { printf '%s\n' "$1 is needed, install it with your package manager" >&2; return 1; }
}

crimson_pkg() {
	pkg-config --exists "$1" || { printf '%s\n' "The development files for $1 are missing, install them with your package manager" >&2; return 1; }
}

crimson_each() {
	crimson_rules=$(mktemp) || return 1
	crimson_make=$(mktemp) || return 1
	crimson_extra=
	for crimson_glob in $4; do
		for crimson_file in $crimson_glob; do
			[ -e "$crimson_file" ] && crimson_extra="$crimson_extra $crimson_file"
		done
	done
	crimson_all=
	crimson_sep=$(printf '\037')
	for crimson_from in $1; do
		[ -e "$crimson_from" ] || continue
		crimson_id=$(printf '%s' "${crimson_from%.*}" | tr '/.' '__')
		crimson_name=$(basename "${crimson_from%.*}")
		crimson_to=$(printf '%s' "$2" | sed -e "s${crimson_sep}{id}${crimson_sep}$crimson_id${crimson_sep}g" -e "s${crimson_sep}{name}${crimson_sep}$crimson_name${crimson_sep}g")
		crimson_command=$(printf '%s' "$3" | sed -e 's/\$/$$/g' -e "s${crimson_sep}{from}${crimson_sep}$crimson_from${crimson_sep}g" -e "s${crimson_sep}{to}${crimson_sep}$crimson_to${crimson_sep}g")
		crimson_all="$crimson_all $crimson_to"
		printf '%s: %s%s\n\t@mkdir -p "$(dir $@)"\n\t@echo "==> %s"\n\t%s\n\n' "$crimson_to" "$crimson_from" "$crimson_extra" "$crimson_from" "$crimson_command" >> "$crimson_rules"
	done
	{ printf 'all:%s\n\n' "$crimson_all"; cat "$crimson_rules"; } > "$crimson_make"
	crimson_status=0
	make -s -f "$crimson_make" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" all || crimson_status=$?
	rm -f "$crimson_make" "$crimson_rules"
	return "$crimson_status"
}

export PROGRAM="crimson-build"
export VERSION="0.1.0"
export PREFIX="${HOME}/.local"
export STD="gnu11"
export WARN="-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter"
export PKGS="glib-2.0 gio-2.0"
printf '==> %s\n' 'needs cc'
crimson_needs "cc"
printf '==> %s\n' 'needs make'
crimson_needs "make"
printf '==> %s\n' 'needs pkg-config'
crimson_needs "pkg-config"
printf '==> %s\n' 'pkg glib-2.0 >= 2.74'
crimson_pkg "glib-2.0 >= 2.74"
printf '==> %s\n' 'pkg gio-2.0'
crimson_pkg "gio-2.0"
printf '==> %s\n' 'each src/*.c src/build/*.c'
crimson_each "src/*.c src/build/*.c" "build/obj/{id}.o" "cc -std=${STD} \$WARN -O2 -g -Isrc \$(pkg-config --cflags \$PKGS | sed \"s/-I/-isystem /g\") -c {from} -o {to}" "src/build/*.h"
printf '==> %s\n' 'cc -o build/${PROGRAM} build/obj/*.o $(pkg-config --libs $PKGS) -lm'
cc -o build/${PROGRAM} build/obj/*.o $(pkg-config --libs $PKGS) -lm
