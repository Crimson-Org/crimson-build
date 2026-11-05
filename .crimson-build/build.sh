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
	pkg-config --exists "$1" && return 0
	pkg-config --print-errors --exists "$1" >&2 || true
	printf '%s\n' 'Install the missing development files with your package manager' >&2
	return 1
}

crimson_each() {
	command -v make >/dev/null 2>&1 || { printf '%s\n' 'The each step needs make, install it with your package manager' >&2; return 1; }
	crimson_work=$(mktemp -d) || return 1
	trap 'rm -rf "$crimson_work"' EXIT
	trap 'exit 130' INT TERM
	export CRIMSON_EACH_TO="$2" CRIMSON_EACH_RUN="$3"
	crimson_dir=$(dirname "${2%%\{id\}*}x")
	export CRIMSON_EACH_LIST="$crimson_dir/.crimson-each-$(printf '%s' "$2" | cksum | cut -d' ' -f1)"
	# A changed command, or a changed variable the command names, makes every result of this step stale
	crimson_settings=$(printf '%s\n' "$3"
		for crimson_name in $(printf '%s' "$3" | grep -o '\${\{0,1\}[A-Za-z_][A-Za-z0-9_]*' | tr -d '${' | sort -u); do
			crimson_value=$(eval "printf '%s' \"\${$crimson_name}\"")
			printf '%s=%s\n' "$crimson_name" "$crimson_value"
		done)
	export CRIMSON_EACH_KEY="$(printf '%s' "$crimson_settings" | cksum | cut -d' ' -f1)"
	CRIMSON_EACH_EXTRA=
	for crimson_glob in $4; do
		for crimson_file in $crimson_glob; do
			[ -e "$crimson_file" ] && CRIMSON_EACH_EXTRA="$CRIMSON_EACH_EXTRA $crimson_file"
		done
	done
	export CRIMSON_EACH_EXTRA
	for crimson_glob in $1; do
		for crimson_file in $crimson_glob; do
			[ -e "$crimson_file" ] && printf '%s\n' "$crimson_file"
		done
	done > "$crimson_work/sources"
	# Every source that an each step builds into the same place, so a result no step makes any more goes away
	for crimson_glob in $5; do
		for crimson_file in $crimson_glob; do
			[ -e "$crimson_file" ] && printf '%s\n' "$crimson_file"
		done
	done > "$crimson_work/known"

	mkdir -p "$crimson_dir"
	[ -f "$CRIMSON_EACH_LIST" ] || : > "$CRIMSON_EACH_LIST"
	awk -v work="$crimson_work" '
		function replace(text, from, to,    result, at) {
			result = ""
			while ((at = index(text, from)) > 0) {
				result = result substr(text, 1, at - 1) to
				text = substr(text, at + length(from))
			}
			return result text
		}
		function make_text(text) { return replace(text, "$", "$$") }
		FILENAME == work "/known" { known[$0] = 1; next }
		FILENAME == work "/sources" {
			if ($0 in to_of) next
			base = $0
			if (match(base, /\.[^.]*$/)) base = substr(base, 1, RSTART - 1)
			name = base
			sub(/.*\//, "", name)
			id = base
			gsub(/[\/.]/, "_", id)
			to = replace(replace(ENVIRON["CRIMSON_EACH_TO"], "{id}", id), "{name}", name)
			to_of[$0] = to
			order[++count] = $0
			next
		}
		{
			split($0, entry, "\t")
			if (entry[1] in to_of) {
				if (entry[3] != ENVIRON["CRIMSON_EACH_KEY"]) print entry[2] > (work "/stale")
			} else if (entry[1] in known) {
				print > (work "/kept")
			} else {
				print entry[2] > (work "/stale")
			}
		}
		END {
			printf "all:" > (work "/make")
			for (index_ = 1; index_ <= count; index_++) printf " %s", make_text(to_of[order[index_]]) > (work "/make")
			printf "\n" > (work "/make")
			for (index_ = 1; index_ <= count; index_++) {
				from = order[index_]
				to = to_of[from]
				command = replace(replace(make_text(ENVIRON["CRIMSON_EACH_RUN"]), "{from}", make_text(from)), "{to}", make_text(to))
				label = "\047==> " replace(from, "\047", "\047\\\047\047") "\047"
				printf "\n%s: %s%s\n\t@mkdir -p \"$(dir $@)\"\n\t@printf \047%%s\\n\047 %s\n\t%s\n", make_text(to), make_text(from), make_text(ENVIRON["CRIMSON_EACH_EXTRA"]), make_text(label), command > (work "/make")
				printf "%s\t%s\t%s\n", from, to, ENVIRON["CRIMSON_EACH_KEY"] > (work "/kept")
			}
		}' "$crimson_work/known" "$crimson_work/sources" "$CRIMSON_EACH_LIST"

	if [ -f "$crimson_work/stale" ]; then
		while IFS= read -r crimson_file; do
			rm -f "$crimson_file"
		done < "$crimson_work/stale"
	fi
	sort "$crimson_work/kept" > "$CRIMSON_EACH_LIST" 2>/dev/null || : > "$CRIMSON_EACH_LIST"
	crimson_status=0
	make -s -f "$crimson_work/make" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" all || crimson_status=$?
	rm -rf "$crimson_work"
	trap - EXIT INT TERM
	return "$crimson_status"
}

export PROGRAM="crimson-build"
export VERSION="0.1.0"
export PREFIX="${HOME}/.local"
export STD="gnu11"
export WARN="-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter"
export OPT="-O2 -g"
export DEFINES="-DPROGRAM_VERSION=\"${VERSION}\""
export PKGS="glib-2.0 >= 2.74"
printf '==> %s\n' 'needs cc, pkg-config'
crimson_needs "cc"
crimson_needs "pkg-config"
printf '==> %s\n' 'pkg ${PKGS}'
crimson_pkg "${PKGS}"
printf '==> %s\n' 'each src/*.c src/build/*.c'
crimson_each "src/*.c src/build/*.c" "build/obj/{id}.o" 'cc -std=${STD} ${WARN} ${OPT} ${DEFINES} -Isrc $(pkg-config --cflags "${PKGS}" | sed "s/-I/-isystem /g") -c {from} -o {to}' "src/build/*.h" "src/*.c src/build/*.c"
printf '==> %s\n' 'cc -o build/${PROGRAM} build/obj/*.o $(pkg-config --libs "${PKGS}") -lm'
cc -o build/${PROGRAM} build/obj/*.o $(pkg-config --libs "${PKGS}") -lm
