#include <string.h>

#include <glib/gstdio.h>

#include "build/json.h"
#include "build/tasks.h"

#define GENERATED_MARKER "Generated from " TASK_FILE_NAME " by Crimson Build"
#define MARKER_SEARCH_BYTES 400
#define SHELL_EXTENSION ".sh"
#define WINDOWS_EXTENSION ".cmd"
#define EXECUTABLE_MODE 0755
#define FOLDER_MODE 0755
/* A step such as copy takes up to two arguments, such as from and to */
#define STEP_ARGUMENTS 2
/* Variables are written ${NAME} */
#define VARIABLE_OPEN "${"
#define VARIABLE_OPEN_LENGTH (sizeof VARIABLE_OPEN - 1)
#define EACH_WINDOWS_MARKER "<# crimson_each #>"

/* The each step's from, to, run, also and the sources of every each step building into the same place, handed to
 * its Windows runner */
static const char *const EACH_WINDOWS_VARIABLES[] = {"CRIMSON_EACH_FROM", "CRIMSON_EACH_TO", "CRIMSON_EACH_RUN",
	"CRIMSON_EACH_ALSO", "CRIMSON_EACH_SHARED"};
#define ERROR_DOMAIN g_quark_from_static_string("crimson-build")

/* Characters that cmd reads as commands, unless a caret comes first */
#define WINDOWS_SPECIAL_CHARACTERS "^&|<>()"

/* Where a piece of text goes in a Windows script */
typedef enum {
	/* A command as written, where quotes and %NAME% keep their Windows meaning */
	WINDOWS_COMMAND,
	/* The value in set "NAME=value", which keeps its quotes and its percent signs */
	WINDOWS_VALUE,
	/* Text inside quotes, which cannot hold a quote itself */
	WINDOWS_ARGUMENT,
	/* A path inside quotes, with / turned to \ */
	WINDOWS_PATH,
} WindowsText;

/* The two kinds of script made for each task */
typedef enum {
	SCRIPT_SHELL,
	SCRIPT_WINDOWS,
	SCRIPT_KINDS,
} ScriptKind;

/* The names of the script kinds in a task file's "systems" list, and their file endings */
static const char *const SYSTEM_NAMES[SCRIPT_KINDS] = {"unix", "windows"};
static const char *const SCRIPT_EXTENSIONS[SCRIPT_KINDS] = {SHELL_EXTENSION, WINDOWS_EXTENSION};

/* A script being written, with the helper functions its steps call */
typedef struct {
	ScriptKind kind;
	GString *steps;
	guint used_helpers;
	gboolean uses_each;
	/* Each task name, so calls to other tasks can be checked */
	const JsonValue *tasks;
} Script;

/* Steps that call a small helper function in each script, which keeps the step lines readable */
typedef struct {
	const char *name;
	/* Member names of the two arguments, or NULL for a step taking one path or a list of paths */
	const char *fields[STEP_ARGUMENTS];
	gboolean are_paths[STEP_ARGUMENTS];
	const char *shell_helper, *windows_helper;
} FileStep;

static const FileStep FILE_STEPS[] = {
	{"copy", {"from", "to"}, {TRUE, TRUE},
		"crimson_copy() {\n"
		"\tif [ -d \"$1\" ]; then\n"
		"\t\tmkdir -p \"$2\" && cp -R \"$1/.\" \"$2\"\n"
		"\telse\n"
		"\t\tmkdir -p \"$(dirname \"$2\")\" && cp \"$1\" \"$2\"\n"
		"\tfi\n"
		"}\n",
		":crimson_copy\n"
		"if exist \"%~1\\*\" (\n"
		"\txcopy \"%~1\" \"%~2\\\" /e /i /y /q >nul\n"
		"\texit /b\n"
		")\n"
		"if not exist \"%~dp2\" mkdir \"%~dp2\"\n"
		"copy /y \"%~1\" \"%~2\" >nul\n"
		"exit /b\n"},
	{"move", {"from", "to"}, {TRUE, TRUE},
		"crimson_move() {\n"
		"\tmkdir -p \"$(dirname \"$2\")\" && mv \"$1\" \"$2\"\n"
		"}\n",
		":crimson_move\n"
		"if not exist \"%~dp2\" mkdir \"%~dp2\"\n"
		"move /y \"%~1\" \"%~2\" >nul\n"
		"exit /b\n"},
	{"remove", {NULL, NULL}, {TRUE, FALSE},
		"crimson_remove() {\n"
		"\trm -rf \"$1\"\n"
		"}\n",
		":crimson_remove\n"
		"if exist \"%~1\\*\" (\n"
		"\trmdir /s /q \"%~1\"\n"
		"\texit /b\n"
		")\n"
		"if exist \"%~1\" del /f /q \"%~1\"\n"
		"exit /b 0\n"},
	{"mkdir", {NULL, NULL}, {TRUE, FALSE},
		"crimson_mkdir() {\n"
		"\tmkdir -p \"$1\"\n"
		"}\n",
		":crimson_mkdir\n"
		"if not exist \"%~1\" mkdir \"%~1\"\n"
		"exit /b\n"},
	{"archive", {"from", "to"}, {TRUE, TRUE},
		"crimson_archive() {\n"
		"\tmkdir -p \"$(dirname \"$2\")\"\n"
		"\tarchive_path=$(cd \"$(dirname \"$2\")\" && pwd)/$(basename \"$2\")\n"
		"\trm -f \"$archive_path\"\n"
		"\tcase \"$2\" in\n"
		"\t*.zip)\n"
		"\t\tif command -v zip >/dev/null 2>&1; then\n"
		"\t\t\t(cd \"$(dirname \"$1\")\" && zip -qr \"$archive_path\" \"$(basename \"$1\")\")\n"
		"\t\telif command -v bsdtar >/dev/null 2>&1; then\n"
		"\t\t\tbsdtar -a -cf \"$archive_path\" -C \"$(dirname \"$1\")\" \"$(basename \"$1\")\"\n"
		"\t\telse\n"
		"\t\t\techo 'Making a zip archive needs zip or bsdtar' >&2\n"
		"\t\t\treturn 1\n"
		"\t\tfi ;;\n"
		"\t*.tar.gz | *.tgz) tar -czf \"$archive_path\" -C \"$(dirname \"$1\")\" \"$(basename \"$1\")\" ;;\n"
		"\t*.tar.xz) tar -cJf \"$archive_path\" -C \"$(dirname \"$1\")\" \"$(basename \"$1\")\" ;;\n"
		"\t*.tar.bz2) tar -cjf \"$archive_path\" -C \"$(dirname \"$1\")\" \"$(basename \"$1\")\" ;;\n"
		"\t*) tar -cf \"$archive_path\" -C \"$(dirname \"$1\")\" \"$(basename \"$1\")\" ;;\n"
		"\tesac\n"
		"}\n",
		":crimson_archive\n"
		"if not exist \"%~dp2\" mkdir \"%~dp2\"\n"
		"if exist \"%~2\" del /f /q \"%~2\"\n"
		"tar -a -cf \"%~f2\" -C \"%~dp1.\" \"%~nx1\"\n"
		"exit /b\n"},
	{"extract", {"from", "to"}, {TRUE, TRUE},
		"crimson_extract() {\n"
		"\tmkdir -p \"$2\"\n"
		"\tcase \"$1\" in\n"
		"\t*.zip)\n"
		"\t\tif command -v unzip >/dev/null 2>&1; then\n"
		"\t\t\tunzip -oq \"$1\" -d \"$2\"\n"
		"\t\telif command -v bsdtar >/dev/null 2>&1; then\n"
		"\t\t\tbsdtar -xf \"$1\" -C \"$2\"\n"
		"\t\telse\n"
		"\t\t\techo 'Unpacking a zip archive needs unzip or bsdtar' >&2\n"
		"\t\t\treturn 1\n"
		"\t\tfi ;;\n"
		"\t*) tar -xf \"$1\" -C \"$2\" ;;\n"
		"\tesac\n"
		"}\n",
		":crimson_extract\n"
		"if not exist \"%~2\" mkdir \"%~2\"\n"
		"tar -xf \"%~1\" -C \"%~2\"\n"
		"exit /b\n"},
	{"download", {"url", "to"}, {FALSE, TRUE},
		"crimson_download() {\n"
		"\tmkdir -p \"$(dirname \"$2\")\"\n"
		"\tif command -v curl >/dev/null 2>&1; then\n"
		"\t\tcurl -fsSL -o \"$2\" \"$1\"\n"
		"\telif command -v wget >/dev/null 2>&1; then\n"
		"\t\twget -q -O \"$2\" \"$1\"\n"
		"\telse\n"
		"\t\techo 'Downloading needs curl or wget' >&2\n"
		"\t\treturn 1\n"
		"\tfi\n"
		"}\n",
		":crimson_download\n"
		"if not exist \"%~dp2\" mkdir \"%~dp2\"\n"
		"curl -fsSL -o \"%~2\" \"%~1\"\n"
		"exit /b\n"},
	{"upload", {"from", "to"}, {TRUE, FALSE},
		"crimson_upload() {\n"
		"\tscp -r \"$1\" \"$2\"\n"
		"}\n",
		":crimson_upload\n"
		"scp -r \"%~1\" \"%~2\"\n"
		"exit /b\n"},
	{"needs", {NULL, NULL}, {FALSE, FALSE},
		"crimson_needs() {\n"
		"\tcommand -v \"$1\" >/dev/null 2>&1 || { printf '%s\\n' \"$1 is needed, install it with your package manager\" >&2; return 1; }\n"
		"}\n",
		":crimson_needs\n"
		"where \"%~1\" >nul 2>&1 || (echo %~1 is needed, install it first & exit /b 1)\n"
		"exit /b 0\n"},
	{"pkg", {NULL, NULL}, {FALSE, FALSE},
		"crimson_pkg() {\n"
		"\tpkg-config --exists \"$1\" && return 0\n"
		"\tpkg-config --print-errors --exists \"$1\" >&2 || true\n"
		"\tprintf '%s\\n' 'Install the missing development files with your package manager' >&2\n"
		"\treturn 1\n"
		"}\n",
		/* The package list can hold >= and <, which echo would take as a redirect, so only pkg-config names it */
		":crimson_pkg\n"
		"pkg-config --exists \"%~1\" && exit /b 0\n"
		"pkg-config --print-errors --exists \"%~1\"\n"
		"echo Install the missing development files with your package manager\n"
		"exit /b 1\n"},
};

/* The each step, which hands make a rule for every matched file, so make runs only what is out of date, on every
 * core. A list in the output folder remembers what each result was built from and with which command */
static const char *const EACH_SHELL_HELPER[] = {
	"crimson_each() {\n"
	"\tcommand -v make >/dev/null 2>&1 || { printf '%s\\n' 'The each step needs make, install it with your package manager' >&2; return 1; }\n"
	"\tcrimson_work=$(mktemp -d) || return 1\n"
	"\ttrap 'rm -rf \"$crimson_work\"' EXIT\n"
	"\ttrap 'exit 130' INT TERM\n"
	"\texport CRIMSON_EACH_TO=\"$2\" CRIMSON_EACH_RUN=\"$3\"\n"
	"\tcrimson_dir=$(dirname \"${2%%\\{id\\}*}x\")\n"
	"\texport CRIMSON_EACH_LIST=\"$crimson_dir/.crimson-each-$(printf '%s' \"$2\" | cksum | cut -d' ' -f1)\"\n"
	"\t# A changed command, or a changed variable the command names, makes every result of this step stale\n"
	"\tcrimson_settings=$(printf '%s\\n' \"$3\"\n"
	"\t\tfor crimson_name in $(printf '%s' \"$3\" | grep -o '\\${\\{0,1\\}[A-Za-z_][A-Za-z0-9_]*' | tr -d '${' | sort -u); do\n"
	"\t\t\tcrimson_value=$(eval \"printf '%s' \\\"\\${$crimson_name}\\\"\")\n"
	"\t\t\tprintf '%s=%s\\n' \"$crimson_name\" \"$crimson_value\"\n"
	"\t\tdone)\n"
	"\texport CRIMSON_EACH_KEY=\"$(printf '%s' \"$crimson_settings\" | cksum | cut -d' ' -f1)\"\n"
	"\tCRIMSON_EACH_EXTRA=\n"
	"\tfor crimson_glob in $4; do\n"
	"\t\tfor crimson_file in $crimson_glob; do\n"
	"\t\t\t[ -e \"$crimson_file\" ] && CRIMSON_EACH_EXTRA=\"$CRIMSON_EACH_EXTRA $crimson_file\"\n"
	"\t\tdone\n"
	"\tdone\n"
	"\texport CRIMSON_EACH_EXTRA\n"
	"\tfor crimson_glob in $1; do\n"
	"\t\tfor crimson_file in $crimson_glob; do\n"
	"\t\t\t[ -e \"$crimson_file\" ] && printf '%s\\n' \"$crimson_file\"\n"
	"\t\tdone\n"
	"\tdone > \"$crimson_work/sources\"\n"
	"\t# Every source that an each step builds into the same place, so a result no step makes any more goes away\n"
	"\tfor crimson_glob in $5; do\n"
	"\t\tfor crimson_file in $crimson_glob; do\n"
	"\t\t\t[ -e \"$crimson_file\" ] && printf '%s\\n' \"$crimson_file\"\n"
	"\t\tdone\n"
	"\tdone > \"$crimson_work/known\"\n"
	"\n"
	"\tmkdir -p \"$crimson_dir\"\n"
	"\t[ -f \"$CRIMSON_EACH_LIST\" ] || : > \"$CRIMSON_EACH_LIST\"\n"
	"\tawk -v work=\"$crimson_work\" '\n"
	"\t\tfunction replace(text, from, to,    result, at) {\n"
	"\t\t\tresult = \"\"\n"
	"\t\t\twhile ((at = index(text, from)) > 0) {\n"
	"\t\t\t\tresult = result substr(text, 1, at - 1) to\n"
	"\t\t\t\ttext = substr(text, at + length(from))\n"
	"\t\t\t}\n"
	"\t\t\treturn result text\n"
	"\t\t}\n"
	"\t\tfunction make_text(text) { return replace(text, \"$\", \"$$\") }\n"
	"\t\tFILENAME == work \"/known\" { known[$0] = 1; next }\n"
	"\t\tFILENAME == work \"/sources\" {\n"
	"\t\t\tif ($0 in to_of) next\n"
	"\t\t\tbase = $0\n"
	"\t\t\tif (match(base, /\\.[^.]*$/)) base = substr(base, 1, RSTART - 1)\n"
	"\t\t\tname = base\n"
	"\t\t\tsub(/.*\\//, \"\", name)\n"
	"\t\t\tid = base\n"
	"\t\t\tgsub(/[\\/.]/, \"_\", id)\n"
	"\t\t\tto = replace(replace(ENVIRON[\"CRIMSON_EACH_TO\"], \"{id}\", id), \"{name}\", name)\n"
	"\t\t\tto_of[$0] = to\n"
	"\t\t\torder[++count] = $0\n"
	"\t\t\tnext\n"
	"\t\t}\n"
	"\t\t{\n"
	"\t\t\tsplit($0, entry, \"\\t\")\n"
	"\t\t\tif (entry[1] in to_of) {\n"
	"\t\t\t\tif (entry[3] != ENVIRON[\"CRIMSON_EACH_KEY\"]) print entry[2] > (work \"/stale\")\n"
	"\t\t\t} else if (entry[1] in known) {\n"
	"\t\t\t\tprint > (work \"/kept\")\n"
	"\t\t\t} else {\n"
	"\t\t\t\tprint entry[2] > (work \"/stale\")\n"
	"\t\t\t}\n"
	"\t\t}\n"
	"\t\tEND {\n"
	"\t\t\tprintf \"all:\" > (work \"/make\")\n"
	"\t\t\tfor (index_ = 1; index_ <= count; index_++) printf \" %s\", make_text(to_of[order[index_]]) > (work \"/make\")\n"
	"\t\t\tprintf \"\\n\" > (work \"/make\")\n"
	"\t\t\tfor (index_ = 1; index_ <= count; index_++) {\n"
	"\t\t\t\tfrom = order[index_]\n"
	"\t\t\t\tto = to_of[from]\n"
	"\t\t\t\tcommand = replace(replace(make_text(ENVIRON[\"CRIMSON_EACH_RUN\"]), \"{from}\", make_text(from)), \"{to}\", make_text(to))\n"
	"\t\t\t\tlabel = \"\\047==> \" replace(from, \"\\047\", \"\\047\\\\\\047\\047\") \"\\047\"\n"
	"\t\t\t\tprintf \"\\n%s: %s%s\\n\\t@mkdir -p \\\"$(dir $@)\\\"\\n\\t@printf \\047%%s\\\\n\\047 %s\\n\\t%s\\n\", make_text(to), make_text(from), make_text(ENVIRON[\"CRIMSON_EACH_EXTRA\"]), make_text(label), command > (work \"/make\")\n"
	"\t\t\t\tprintf \"%s\\t%s\\t%s\\n\", from, to, ENVIRON[\"CRIMSON_EACH_KEY\"] > (work \"/kept\")\n"
	"\t\t\t}\n"
	"\t\t}' \"$crimson_work/known\" \"$crimson_work/sources\" \"$CRIMSON_EACH_LIST\"\n"
	"\n",
	"\tif [ -f \"$crimson_work/stale\" ]; then\n"
	"\t\twhile IFS= read -r crimson_file; do\n"
	"\t\t\trm -f \"$crimson_file\"\n"
	"\t\tdone < \"$crimson_work/stale\"\n"
	"\tfi\n"
	"\tsort \"$crimson_work/kept\" > \"$CRIMSON_EACH_LIST\" 2>/dev/null || : > \"$CRIMSON_EACH_LIST\"\n"
	"\tcrimson_status=0\n"
	"\tmake -s -f \"$crimson_work/make\" -j\"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)\" all || crimson_status=$?\n"
	"\trm -rf \"$crimson_work\"\n"
	"\ttrap - EXIT INT TERM\n"
	"\treturn \"$crimson_status\"\n"
	"}\n"
};

/* cmd cannot do the each step by itself, so the step runs in PowerShell from the end of the same script, which cmd
 * never reaches, since every path through the script exits before */
static const char EACH_WINDOWS_HELPER[] = ":crimson_each\n"
	"powershell -NoProfile -ExecutionPolicy Bypass -Command \"$text = [IO.File]::ReadAllText('%~f0'); "
	"Invoke-Expression $text.Substring($text.LastIndexOf('" EACH_WINDOWS_MARKER "'))\"\n"
	"exit /b\n";

static const char *const EACH_WINDOWS_RUNNER[] = {
	EACH_WINDOWS_MARKER "\n"
	"# Runs one each step for crimson_each above, which cmd cannot do by itself\n"
	"$ErrorActionPreference = 'Stop'\n"
	"$root = (Get-Location).Path\n"
	"\n"
	"function Get-Matches([string] $patterns) {\n"
	"\tforeach ($pattern in $patterns.Split(' ', [StringSplitOptions]::RemoveEmptyEntries)) {\n"
	"\t\tforeach ($file in Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue) {\n"
	"\t\t\t(Resolve-Path -LiteralPath $file.FullName -Relative) -replace '^\\.[\\\\/]', '' -replace '\\\\', '/'\n"
	"\t\t}\n"
	"\t}\n"
	"}\n"
	"\n"
	"function Get-FullPath([string] $path) {\n"
	"\treturn [IO.Path]::Combine($root, $path)\n"
	"}\n"
	"\n"
	"function Get-Key([string] $text) {\n"
	"\t$hash = [Security.Cryptography.MD5]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($text))\n"
	"\treturn [BitConverter]::ToString($hash, 0, 4).Replace('-', '')\n"
	"}\n"
	"\n"
	"function Remove-Result([string] $path) {\n"
	"\tRemove-Item -LiteralPath (Get-FullPath $path) -Force -ErrorAction SilentlyContinue\n"
	"}\n"
	"\n"
	"$template = $env:CRIMSON_EACH_TO\n"
	"$command = $env:CRIMSON_EACH_RUN\n"
	"# cmd has filled in the command's variables already, so a changed variable changes the key too\n"
	"$key = Get-Key $command\n"
	"$folder = Split-Path -Parent ($template.Substring(0, $template.IndexOf('{id}')) + 'x')\n"
	"if (-not $folder) { $folder = '.' }\n"
	"$list = Get-FullPath \"$folder/.crimson-each-$(Get-Key $template)\"\n"
	"[void][IO.Directory]::CreateDirectory((Get-FullPath $folder))\n"
	"\n"
	"$newestExtra = [DateTime]::MinValue\n"
	"foreach ($extra in Get-Matches $env:CRIMSON_EACH_ALSO) {\n"
	"\t$time = [IO.File]::GetLastWriteTimeUtc((Get-FullPath $extra))\n"
	"\tif ($time -gt $newestExtra) { $newestExtra = $time }\n"
	"}\n"
	"\n"
	"$mine = [ordered]@{}\n"
	"foreach ($source in @(Get-Matches $env:CRIMSON_EACH_FROM | Sort-Object -Unique)) {\n"
	"\t$base = $source\n"
	"\tif ($source.LastIndexOf('.') -ge 0) { $base = $source.Substring(0, $source.LastIndexOf('.')) }\n"
	"\t$mine[$source] = $template.Replace('{id}', $base.Replace('/', '_').Replace('.', '_')).Replace('{name}', (Split-Path -Leaf $base))\n"
	"}\n"
	"\n"
	"# Every source that an each step builds into the same place, so a result no step makes any more goes away\n"
	"$known = @{}\n"
	"foreach ($source in Get-Matches $env:CRIMSON_EACH_SHARED) { $known[$source] = $true }\n"
	"$entries = @()\n"
	"if (Test-Path -LiteralPath $list) {\n"
	"\tforeach ($line in [IO.File]::ReadAllLines($list)) {\n"
	"\t\t$source, $to, $oldKey = $line.Split(\"`t\")\n"
	"\t\tif ($mine.Contains($source)) {\n"
	"\t\t\t# A changed command, or a changed variable in it, makes every result of this step stale\n"
	"\t\t\tif ($oldKey -ne $key) { Remove-Result $to }\n"
	"\t\t} elseif ($known.Contains($source)) {\n"
	"\t\t\t$entries += $line\n"
	"\t\t} else {\n"
	"\t\t\tRemove-Result $to\n"
	"\t\t}\n"
	"\t}\n"
	"}\n"
	"foreach ($source in $mine.Keys) { $entries += \"$source`t$($mine[$source])`t$key\" }\n"
	"[IO.File]::WriteAllLines($list, [string[]]($entries | Sort-Object))\n"
	"\n"
	"$work = @()\n"
	"foreach ($source in $mine.Keys) {\n"
	"\t$to = $mine[$source]\n"
	"\t$newest = [IO.File]::GetLastWriteTimeUtc((Get-FullPath $source))\n"
	"\tif ($newestExtra -gt $newest) { $newest = $newestExtra }\n"
	"\t$target = Get-FullPath $to\n"
	"\tif ((Test-Path -LiteralPath $target) -and [IO.File]::GetLastWriteTimeUtc($target) -ge $newest) { continue }\n"
	"\t$run = $command.Replace('{from}', $source.Replace('/', '\\')).Replace('{to}', $to.Replace('/', '\\'))\n"
	"\t$work += [pscustomobject]@{ From = $source; To = $to; Command = $run }\n"
	"}\n"
	"\n",
	"# Runs as many commands at once as there are processors, and starts no new one after a failure\n"
	"$running = New-Object 'Collections.Generic.List[Diagnostics.Process]'\n"
	"$hasFailed = $false\n"
	"foreach ($item in $work) {\n"
	"\twhile ($running.Count -ge [Environment]::ProcessorCount) {\n"
	"\t\t$running[0].WaitForExit()\n"
	"\t\tif ($running[0].ExitCode -ne 0) { $hasFailed = $true }\n"
	"\t\t$running.RemoveAt(0)\n"
	"\t}\n"
	"\tif ($hasFailed) { break }\n"
	"\t[void][IO.Directory]::CreateDirectory((Split-Path -Parent (Get-FullPath $item.To)))\n"
	"\tWrite-Host \"==> $($item.From)\"\n"
	"\t$start = New-Object Diagnostics.ProcessStartInfo $env:ComSpec, \"/d /s /c `\"$($item.Command)`\"\"\n"
	"\t$start.UseShellExecute = $false\n"
	"\t$start.WorkingDirectory = $root\n"
	"\t$running.Add([Diagnostics.Process]::Start($start))\n"
	"}\n"
	"foreach ($process in $running) {\n"
	"\t$process.WaitForExit()\n"
	"\tif ($process.ExitCode -ne 0) { $hasFailed = $true }\n"
	"}\n"
	"if ($hasFailed) { exit 1 }\n"
	"exit 0\n"
};


/* Files that tell which build command a new project most likely uses */
static const struct {
	const char *file, *command;
} BUILD_HINTS[] = {
	{"build.sh", "sh build.sh"}, {"meson.build", "meson compile -C build"},
	{"Cargo.toml", "cargo build"}, {"CMakeLists.txt", "cmake --build build"}, {"Makefile", "make"},
	{"go.mod", "go build ./..."}, {"package.json", "npm run build"}, {"build.zig", "zig build"},
};


/* ==================================== Text conversion ==================================== */


static gboolean is_variable_start(const char *text) {
	return strncmp(text, VARIABLE_OPEN, VARIABLE_OPEN_LENGTH) == 0;
}


/* Double quotes a value for the shell while keeping ${NAME} variables working */
static void append_shell_quoted(GString *output, const char *text) {
	g_string_append_c(output, '"');
	for (const char *character = text; *character; character++) {
		if (strchr("\"\\`", *character) || (*character == '$' && !is_variable_start(character)))
			g_string_append_c(output, '\\');
		g_string_append_c(output, *character);
	}
	g_string_append_c(output, '"');
}


static void append_single_quoted(GString *output, const char *text) {
	g_string_append_c(output, '\'');
	for (const char *character = text; *character; character++) {
		if (*character == '\'')
			g_string_append(output, "'\\''");
		else
			g_string_append_c(output, *character);
	}
	g_string_append_c(output, '\'');
}


/* Rewrites ${NAME} as %NAME%, and fits the rest to where the text goes */
static void append_windows_text(GString *output, const char *text, WindowsText place) {
	gboolean is_inside_quotes = TRUE;
	for (const char *character = text; *character; character++) {
		const char *close = is_variable_start(character) ? strchr(character, '}') : NULL;
		if (close) {
			g_string_append_c(output, '%');
			g_string_append_len(output, character + VARIABLE_OPEN_LENGTH, close - character - VARIABLE_OPEN_LENGTH);
			g_string_append_c(output, '%');
			character = close;
		} else if (*character == '/' && place == WINDOWS_PATH) {
			g_string_append_c(output, '\\');
		} else if (*character == '%' && place != WINDOWS_COMMAND) {
			g_string_append(output, "%%");
		} else if (*character == '"' && (place == WINDOWS_ARGUMENT || place == WINDOWS_PATH)) {
			continue;
		} else {
			/* Each quote in a value switches cmd in or out of quoted text, and outside it these characters would
			 * start another command, so they get the caret that makes them plain text */
			if (*character == '"')
				is_inside_quotes = !is_inside_quotes;
			else if (place == WINDOWS_VALUE && !is_inside_quotes && strchr(WINDOWS_SPECIAL_CHARACTERS, *character))
				g_string_append_c(output, '^');
			g_string_append_c(output, *character);
		}
	}
}


static void append_windows_quoted(GString *output, const char *text, gboolean is_path) {
	g_string_append_c(output, '"');
	append_windows_text(output, text, is_path ? WINDOWS_PATH : WINDOWS_ARGUMENT);
	g_string_append_c(output, '"');
}


static void append_windows_setting(GString *output, const char *name, const char *value) {
	g_string_append_printf(output, "set \"%s=", name);
	append_windows_text(output, value, WINDOWS_VALUE);
	g_string_append(output, "\"\n");
}


/* Writes text that echo prints as is, apart from variables when they are wanted, since cmd treats these characters
 * as commands */
static void append_windows_echo(GString *output, const char *text, gboolean expands_variables) {
	g_string_append(output, "echo(");
	for (const char *character = text; *character; character++) {
		const char *close = expands_variables && is_variable_start(character) ? strchr(character, '}') : NULL;
		if (close) {
			g_string_append_printf(output, "%%%.*s%%", (int)(close - character - VARIABLE_OPEN_LENGTH), character + VARIABLE_OPEN_LENGTH);
			character = close;
			continue;
		}
		if (strchr(WINDOWS_SPECIAL_CHARACTERS, *character))
			g_string_append_c(output, '^');
		if (*character == '%')
			g_string_append_c(output, '%');
		g_string_append_c(output, *character);
	}
	g_string_append_c(output, '\n');
}


static gboolean is_valid_name(const char *name, gboolean allows_punctuation) {
	if (!name || !*name || *name == '.' || g_ascii_isdigit(*name))
		return FALSE;
	for (const char *character = name; *character; character++)
		if (!g_ascii_isalnum(*character) && *character != '_'
			&& !(allows_punctuation && (*character == '-' || *character == '.')))
			return FALSE;
	return TRUE;
}


/* ==================================== Steps ==================================== */


static const JsonValue *get_task_steps(const JsonValue *task) {
	if (task->kind == JSON_OBJECT)
		return get_json_member(task, "steps");
	return task;
}


static void append_label(Script *script, const char *label) {
	if (script->kind == SCRIPT_SHELL) {
		g_string_append(script->steps, "printf '==> %s\\n' ");
		append_single_quoted(script->steps, label);
		g_string_append_c(script->steps, '\n');
	} else {
		g_autofree char *line = g_strconcat("==> ", label, NULL);
		append_windows_echo(script->steps, line, FALSE);
	}
}


/* Stops a Windows script when the step before failed, which the shell script gets from set -e */
static void append_failure_check(Script *script) {
	if (script->kind == SCRIPT_WINDOWS)
		g_string_append(script->steps, "if %errorlevel% neq 0 exit /b %errorlevel%\n");
}


static void append_skipped(Script *script) {
	if (script->kind == SCRIPT_SHELL)
		g_string_append(script->steps, "echo 'Skipped on this system'\n");
	else
		g_string_append(script->steps, "echo Skipped on this system\n");
}


static void append_shell_run(Script *script, const JsonValue *argument) {
	const char *unix_command = get_json_string(argument, NULL);
	unix_command = get_json_string(get_json_member(argument, "unix"), unix_command);
	const char *linux_command = get_json_string(get_json_member(argument, "linux"), unix_command);
	const char *macos_command = get_json_string(get_json_member(argument, "macos"), unix_command);
	if (g_strcmp0(linux_command, macos_command) == 0) {
		append_label(script, linux_command ? linux_command : "run");
		if (linux_command)
			g_string_append_printf(script->steps, "%s\n", linux_command);
		else
			append_skipped(script);
		return;
	}
	append_label(script, "run");
	g_string_append(script->steps, "case \"$(uname -s)\" in\nDarwin)\n");
	if (macos_command)
		g_string_append_printf(script->steps, "\t%s\n\t;;\n*)\n", macos_command);
	else
		g_string_append(script->steps, "\techo 'Skipped on this system'\n\t;;\n*)\n");
	if (linux_command)
		g_string_append_printf(script->steps, "\t%s\n\t;;\nesac\n", linux_command);
	else
		g_string_append(script->steps, "\techo 'Skipped on this system'\n\t;;\nesac\n");
}


static gboolean write_run_step(Script *script, const JsonValue *argument, GError **error) {
	if (argument->kind != JSON_STRING && argument->kind != JSON_OBJECT) {
		g_set_error(error, ERROR_DOMAIN, 0, "A run step takes a command or an object of commands per system");
		return FALSE;
	}
	if (script->kind == SCRIPT_SHELL) {
		append_shell_run(script, argument);
		return TRUE;
	}
	const char *command = get_json_string(argument, NULL);
	command = get_json_string(get_json_member(argument, "windows"), command);
	append_label(script, command ? command : "run");
	if (!command) {
		append_skipped(script);
		return TRUE;
	}
	append_windows_text(script->steps, command, WINDOWS_COMMAND);
	g_string_append_c(script->steps, '\n');
	append_failure_check(script);
	return TRUE;
}


static gboolean write_task_step(Script *script, const JsonValue *argument, GError **error) {
	const char *name = get_json_string(argument, NULL);
	if (!name) {
		g_set_error(error, ERROR_DOMAIN, 0, "A task step takes the name of a task");
		return FALSE;
	}
	if (!get_json_member(script->tasks, name)) {
		g_set_error(error, ERROR_DOMAIN, 0, "There is no task \"%s\" in the task file", name);
		return FALSE;
	}
	if (script->kind == SCRIPT_SHELL) {
		g_string_append(script->steps, "sh \"$script_directory/");
		g_string_append_printf(script->steps, "%s" SHELL_EXTENSION "\"\n", name);
	} else {
		g_string_append_printf(script->steps, "call \"%%~dp0%s" WINDOWS_EXTENSION "\"\n", name);
		append_failure_check(script);
	}
	return TRUE;
}


static gboolean write_echo_step(Script *script, const JsonValue *argument, GError **error) {
	const char *text = get_json_string(argument, NULL);
	if (!text) {
		g_set_error(error, ERROR_DOMAIN, 0, "An echo step takes text");
		return FALSE;
	}
	if (script->kind == SCRIPT_SHELL) {
		g_string_append(script->steps, "printf '%s\\n' ");
		append_shell_quoted(script->steps, text);
		g_string_append_c(script->steps, '\n');
	} else {
		append_windows_echo(script->steps, text, TRUE);
	}
	return TRUE;
}


static void append_helper_call(Script *script, const FileStep *step, const char *const *values) {
	g_string_append_printf(script->steps, script->kind == SCRIPT_SHELL ? "crimson_%s" : "call :crimson_%s", step->name);
	for (int index = 0; index < STEP_ARGUMENTS && values[index]; index++) {
		g_string_append_c(script->steps, ' ');
		if (script->kind == SCRIPT_SHELL)
			append_shell_quoted(script->steps, values[index]);
		else
			append_windows_quoted(script->steps, values[index], step->are_paths[index]);
	}
	g_string_append_c(script->steps, '\n');
	append_failure_check(script);
}


static gboolean write_file_step(Script *script, const FileStep *step, const JsonValue *argument, GError **error) {
	script->used_helpers |= 1u << (step - FILE_STEPS);
	if (!step->fields[0]) {
		gboolean is_list = argument->kind == JSON_ARRAY;
		guint count = is_list ? get_json_length(argument) : 1;
		g_autoptr(GString) label = g_string_new(step->name);
		for (guint index = 0; index < count; index++) {
			const char *value = get_json_string(is_list ? get_json_item(argument, index) : argument, NULL);
			if (!value) {
				g_set_error(error, ERROR_DOMAIN, 0, "A %s step takes text or a list of texts", step->name);
				return FALSE;
			}
			g_string_append_printf(label, index ? ", %s" : " %s", value);
		}
		append_label(script, label->str);
		for (guint index = 0; index < count; index++) {
			const char *values[STEP_ARGUMENTS] = {get_json_string(is_list ? get_json_item(argument, index) : argument, NULL)};
			append_helper_call(script, step, values);
		}
		return TRUE;
	}
	const char *values[STEP_ARGUMENTS] = {
		get_json_string(get_json_member(argument, step->fields[0]), NULL),
		get_json_string(get_json_member(argument, step->fields[1]), NULL),
	};
	if (!values[0] || !values[1]) {
		g_set_error(error, ERROR_DOMAIN, 0, "A %s step takes \"%s\" and \"%s\"", step->name, step->fields[0],
			step->fields[1]);
		return FALSE;
	}
	g_autofree char *label = g_strdup_printf("%s %s to %s", step->name, values[0], values[1]);
	append_label(script, label);
	append_helper_call(script, step, values);
	return TRUE;
}


/* Joins one pattern or a list of them with spaces, as the each helpers read them */
static gboolean append_patterns(GString *patterns, const JsonValue *value) {
	gboolean is_list = value->kind == JSON_ARRAY;
	for (guint index = 0; index < (is_list ? get_json_length(value) : 1); index++) {
		const char *pattern = get_json_string(is_list ? get_json_item(value, index) : value, NULL);
		if (!pattern)
			return FALSE;
		g_string_append_printf(patterns, "%s%s", patterns->len ? " " : "", pattern);
	}
	return TRUE;
}


/* Every source pattern of the each steps that build into the same place, in any task, so the step can tell a result
 * that some step still makes from one that nothing makes any more */
static void append_shared_sources(GString *patterns, const JsonValue *tasks, const char *to) {
	for (guint task = 0; task < get_json_length(tasks); task++) {
		const JsonValue *steps = get_task_steps(get_json_item(tasks, task));
		for (guint index = 0; index < get_json_length(steps) && steps->kind == JSON_ARRAY; index++) {
			const JsonValue *each = get_json_member(get_json_item(steps, index), "each");
			const char *from = get_json_string(get_json_member(each, "from"), NULL);
			if (from && g_strcmp0(get_json_string(get_json_member(each, "to"), NULL), to) == 0)
				g_string_append_printf(patterns, "%s%s", patterns->len ? " " : "", from);
		}
	}
}


/* Compiles or converts every file that matches a pattern, only those whose result is older than the file or the
 * extras. A result whose file is gone, or that no step makes any more, is removed, and a step builds all of its
 * results again when the command or the variables the command names change, since make only compares file times */
static gboolean write_each_step(Script *script, const JsonValue *argument, GError **error) {
	const char *from = get_json_string(get_json_member(argument, "from"), NULL);
	const char *to = get_json_string(get_json_member(argument, "to"), NULL);
	const char *run = get_json_string(get_json_member(argument, "run"), NULL);
	if (!from || !to || !run || !strstr(to, "{id}")) {
		g_set_error(error, ERROR_DOMAIN, 0, "An each step takes \"from\", \"to\" holding {id}, and \"run\"");
		return FALSE;
	}
	g_autoptr(GString) extras = g_string_new("");
	const JsonValue *also = get_json_member(argument, "also");
	if (also && !append_patterns(extras, also)) {
		g_set_error(error, ERROR_DOMAIN, 0, "The \"also\" of an each step takes patterns");
		return FALSE;
	}
	g_autoptr(GString) shared = g_string_new("");
	append_shared_sources(shared, script->tasks, to);
	script->uses_each = TRUE;
	g_autofree char *label = g_strdup_printf("each %s", from);
	append_label(script, label);
	const char *values[] = {from, to, run, extras->str, shared->str};
	G_STATIC_ASSERT(G_N_ELEMENTS(values) == G_N_ELEMENTS(EACH_WINDOWS_VARIABLES));
	if (script->kind == SCRIPT_WINDOWS) {
		for (guint index = 0; index < G_N_ELEMENTS(values); index++)
			append_windows_setting(script->steps, EACH_WINDOWS_VARIABLES[index], values[index]);
		g_string_append(script->steps, "call :crimson_each\n");
		append_failure_check(script);
		return TRUE;
	}
	g_string_append(script->steps, "crimson_each");
	for (guint index = 0; index < G_N_ELEMENTS(values); index++) {
		g_string_append_c(script->steps, ' ');
		/* The command reaches the shell as written, so its variables are read when it runs, as in a run step,
		 * while the patterns take their variables at once */
		if (values[index] == run)
			append_single_quoted(script->steps, run);
		else
			append_shell_quoted(script->steps, values[index]);
	}
	g_string_append_c(script->steps, '\n');
	return TRUE;
}


static gboolean write_step(Script *script, const JsonValue *step, GError **error) {
	if (step->kind == JSON_STRING)
		return write_run_step(script, step, error);
	if (step->kind != JSON_OBJECT || get_json_length(step) != 1) {
		g_set_error(error, ERROR_DOMAIN, 0, "Each step is a command or an object with one step kind");
		return FALSE;
	}
	const char *kind = g_ptr_array_index(step->names, 0);
	const JsonValue *argument = get_json_item(step, 0);
	if (strcmp(kind, "run") == 0)
		return write_run_step(script, argument, error);
	if (strcmp(kind, "task") == 0)
		return write_task_step(script, argument, error);
	if (strcmp(kind, "echo") == 0)
		return write_echo_step(script, argument, error);
	if (strcmp(kind, "each") == 0)
		return write_each_step(script, argument, error);
	for (guint index = 0; index < G_N_ELEMENTS(FILE_STEPS); index++)
		if (strcmp(kind, FILE_STEPS[index].name) == 0)
			return write_file_step(script, &FILE_STEPS[index], argument, error);
	g_set_error(error, ERROR_DOMAIN, 0, "Unknown step kind \"%s\"", kind);
	return FALSE;
}


/* ==================================== Scripts ==================================== */


static gboolean write_environment(Script *script, const JsonValue *environment, GError **error) {
	for (guint index = 0; index < get_json_length(environment); index++) {
		const char *name = g_ptr_array_index(environment->names, index);
		const char *value = get_json_string(get_json_item(environment, index), NULL);
		if (!is_valid_name(name, FALSE) || !value) {
			g_set_error(error, ERROR_DOMAIN, 0, "Variable \"%s\" needs a plain name and a text value", name);
			return FALSE;
		}
		if (script->kind == SCRIPT_SHELL) {
			g_string_append_printf(script->steps, "export %s=", name);
			append_shell_quoted(script->steps, value);
			g_string_append_c(script->steps, '\n');
		} else {
			append_windows_setting(script->steps, name, value);
		}
	}
	return TRUE;
}


static gboolean write_dependencies(Script *script, const JsonValue *task, GError **error) {
	const JsonValue *depends = get_json_member(task, "depends");
	if (depends && depends->kind == JSON_STRING)
		return write_task_step(script, depends, error);
	for (guint index = 0; index < get_json_length(depends); index++)
		if (!write_task_step(script, get_json_item(depends, index), error))
			return FALSE;
	return TRUE;
}


static gboolean write_directory_change(Script *script, const JsonValue *task) {
	const char *directory = get_json_string(get_json_member(task, "directory"), NULL);
	if (!directory)
		return TRUE;
	if (script->kind == SCRIPT_SHELL) {
		g_string_append(script->steps, "cd ");
		append_shell_quoted(script->steps, directory);
	} else {
		g_string_append(script->steps, "cd /d ");
		append_windows_quoted(script->steps, directory, TRUE);
	}
	g_string_append_c(script->steps, '\n');
	append_failure_check(script);
	return TRUE;
}


static gboolean write_task_body(Script *script, const JsonValue *root, const JsonValue *task, GError **error) {
	if (task->kind == JSON_OBJECT && !write_dependencies(script, task, error))
		return FALSE;
	if (!write_environment(script, get_json_member(root, "env"), error))
		return FALSE;
	if (task->kind == JSON_OBJECT && !write_environment(script, get_json_member(task, "env"), error))
		return FALSE;
	if (task->kind == JSON_OBJECT)
		write_directory_change(script, task);
	const JsonValue *steps = get_task_steps(task);
	if (steps && steps->kind == JSON_STRING)
		return write_step(script, steps, error);
	for (guint index = 0; index < get_json_length(steps); index++)
		if (!write_step(script, get_json_item(steps, index), error))
			return FALSE;
	return TRUE;
}


static char *assemble_script(Script *script, const char *description) {
	GString *text = g_string_new(NULL);
	if (script->kind == SCRIPT_SHELL) {
		g_string_append(text, "#!/bin/sh\n# " GENERATED_MARKER ", edit that file instead\n");
		if (description)
			g_string_append_printf(text, "# %s\n", description);
		g_string_append(text, "set -eu\nscript_directory=$(cd \"$(dirname \"$0\")\" && pwd)\n"
			"cd \"$script_directory/..\"\n");
	} else {
		g_string_append(text, "@echo off\nrem " GENERATED_MARKER ", edit that file instead\n");
		if (description)
			g_string_append_printf(text, "rem %s\n", description);
		g_string_append(text, "setlocal\ncd /d \"%~dp0..\"\n");
	}
	for (guint index = 0; index < G_N_ELEMENTS(FILE_STEPS) && script->kind == SCRIPT_SHELL; index++)
		if (script->used_helpers & (1u << index))
			g_string_append_printf(text, "\n%s", FILE_STEPS[index].shell_helper);
	if (script->uses_each && script->kind == SCRIPT_SHELL) {
		g_string_append_c(text, '\n');
		for (guint index = 0; index < G_N_ELEMENTS(EACH_SHELL_HELPER); index++)
			g_string_append(text, EACH_SHELL_HELPER[index]);
	}
	g_string_append_printf(text, "\n%s", script->steps->str);
	if (script->kind == SCRIPT_WINDOWS) {
		g_string_append(text, "exit /b 0\n");
		for (guint index = 0; index < G_N_ELEMENTS(FILE_STEPS); index++)
			if (script->used_helpers & (1u << index))
				g_string_append_printf(text, "\n%s", FILE_STEPS[index].windows_helper);
		if (script->uses_each) {
			g_string_append_printf(text, "\n%s\n", EACH_WINDOWS_HELPER);
			for (guint index = 0; index < G_N_ELEMENTS(EACH_WINDOWS_RUNNER); index++)
				g_string_append(text, EACH_WINDOWS_RUNNER[index]);
		}
	}
	if (script->kind == SCRIPT_SHELL)
		return g_string_free(text, FALSE);
	/* Batch files read labels reliably only with Windows line endings */
	g_autofree char *unix_text = g_string_free(text, FALSE);
	g_auto(GStrv) lines = g_strsplit(unix_text, "\n", -1);
	return g_strjoinv("\r\n", lines);
}


static char *make_script(ScriptKind kind, const JsonValue *root, const char *name, GError **error) {
	const JsonValue *tasks = get_json_member(root, "tasks");
	const JsonValue *task = get_json_member(tasks, name);
	Script script = {.kind = kind, .steps = g_string_new(NULL), .tasks = tasks};
	gboolean is_written = write_task_body(&script, root, task, error);
	char *text = is_written ? assemble_script(&script, get_json_string(get_json_member(task, "description"), NULL))
		: NULL;
	g_string_free(script.steps, TRUE);
	if (!is_written)
		g_prefix_error(error, "Task \"%s\": ", name);
	return text;
}


/* ==================================== Task file ==================================== */


char *find_task_project(const char *path, GError **error) {
	g_autofree char *absolute = g_canonicalize_filename(path, NULL);
	g_autofree char *name = g_path_get_basename(absolute);
	g_autofree char *parent = g_path_get_dirname(absolute);
	if (g_file_test(absolute, G_FILE_TEST_IS_DIR))
		return g_strcmp0(name, TASK_DIRECTORY) == 0 ? g_steal_pointer(&parent) : g_steal_pointer(&absolute);
	if (g_file_test(absolute, G_FILE_TEST_IS_REGULAR) && g_strcmp0(name, TASK_FILE_NAME) == 0)
		return g_path_get_dirname(parent);
	g_set_error(error, ERROR_DOMAIN, 0,
		"%s is not a project folder, a " TASK_DIRECTORY " folder or a " TASK_FILE_NAME " file", path);
	return NULL;
}


char *get_task_file_path(const char *project_directory) {
	return g_build_filename(project_directory, TASK_DIRECTORY, TASK_FILE_NAME, NULL);
}


char *get_task_command(const char *project_directory, const char *name) {
	g_autofree char *file_name = g_strconcat(name, SHELL_EXTENSION, NULL);
	g_autofree char *path = g_build_filename(project_directory, TASK_DIRECTORY, file_name, NULL);
	g_autofree char *quoted = g_shell_quote(path);
	return g_strconcat("sh ", quoted, NULL);
}


/* Collects the tasks a task runs first or calls, which must not lead back to itself */
static void collect_called_tasks(const JsonValue *task, GPtrArray *names) {
	if (task->kind != JSON_OBJECT && task->kind != JSON_ARRAY)
		return;
	const JsonValue *depends = get_json_member(task, "depends");
	if (depends && depends->kind == JSON_STRING)
		g_ptr_array_add(names, depends->string);
	for (guint index = 0; index < get_json_length(depends); index++)
		if (get_json_string(get_json_item(depends, index), NULL))
			g_ptr_array_add(names, get_json_item(depends, index)->string);
	const JsonValue *steps = get_task_steps(task);
	for (guint index = 0; index < get_json_length(steps) && steps->kind == JSON_ARRAY; index++) {
		const char *called = get_json_string(get_json_member(get_json_item(steps, index), "task"), NULL);
		if (called)
			g_ptr_array_add(names, (char *)called);
	}
}


static gboolean has_call_loop(const JsonValue *tasks, const char *name, GHashTable *visiting) {
	if (g_hash_table_contains(visiting, name))
		return TRUE;
	const JsonValue *task = get_json_member(tasks, name);
	if (!task)
		return FALSE;
	g_hash_table_add(visiting, (char *)name);
	g_autoptr(GPtrArray) called = g_ptr_array_new();
	collect_called_tasks(task, called);
	for (guint index = 0; index < called->len; index++)
		if (has_call_loop(tasks, g_ptr_array_index(called, index), visiting))
			return TRUE;
	g_hash_table_remove(visiting, name);
	return FALSE;
}


/* A missing value is fine, since every member of a task is optional */
static gboolean is_missing_or(const JsonValue *value, JsonKind kind) {
	return !value || value->kind == kind;
}


static gboolean is_text_or_texts(const JsonValue *value) {
	if (!value || value->kind == JSON_STRING)
		return TRUE;
	if (value->kind != JSON_ARRAY)
		return FALSE;
	for (guint index = 0; index < get_json_length(value); index++)
		if (get_json_item(value, index)->kind != JSON_STRING)
			return FALSE;
	return TRUE;
}


/* Says which part of a task has the wrong kind of value, so a typo stops with a message instead of a crash */
static const char *find_task_problem(const JsonValue *task) {
	if (task->kind == JSON_STRING || task->kind == JSON_ARRAY)
		return NULL;
	if (task->kind != JSON_OBJECT)
		return "is not a command, a list of steps or an object";
	if (!is_missing_or(get_json_member(task, "env"), JSON_OBJECT))
		return "has an \"env\" that is not an object of variables";
	if (!is_text_or_texts(get_json_member(task, "depends")))
		return "has a \"depends\" that is neither a task name nor a list of task names";
	if (!is_missing_or(get_json_member(task, "directory"), JSON_STRING))
		return "has a \"directory\" that is not a path";
	if (!is_missing_or(get_json_member(task, "description"), JSON_STRING))
		return "has a \"description\" that is not text";
	const JsonValue *steps = get_json_member(task, "steps");
	if (steps && steps->kind != JSON_STRING && steps->kind != JSON_ARRAY)
		return "has \"steps\" that are not a command or a list of steps";
	return NULL;
}


/* Which kinds of script the task file asks for, both when it does not say */
static gboolean read_wanted_scripts(const JsonValue *root, gboolean is_wanted[SCRIPT_KINDS], GError **error) {
	const JsonValue *systems = get_json_member(root, "systems");
	for (int kind = 0; kind < SCRIPT_KINDS; kind++)
		is_wanted[kind] = !systems;
	if (!systems)
		return TRUE;
	for (guint index = 0; index < get_json_length(systems) && systems->kind == JSON_ARRAY; index++) {
		const char *name = get_json_string(get_json_item(systems, index), "");
		for (int kind = 0; kind < SCRIPT_KINDS; kind++)
			is_wanted[kind] |= g_strcmp0(name, SYSTEM_NAMES[kind]) == 0;
	}
	for (int kind = 0; kind < SCRIPT_KINDS; kind++)
		if (is_wanted[kind])
			return TRUE;
	g_set_error(error, ERROR_DOMAIN, 0, "\"systems\" needs \"%s\", \"%s\" or both", SYSTEM_NAMES[SCRIPT_SHELL],
		SYSTEM_NAMES[SCRIPT_WINDOWS]);
	return FALSE;
}


static JsonValue *load_task_root(const char *project_directory, GError **error) {
	g_autofree char *path = get_task_file_path(project_directory);
	g_autofree char *text = NULL;
	gsize length = 0;
	if (!g_file_get_contents(path, &text, &length, error))
		return NULL;
	g_autoptr(JsonValue) root = parse_json(text, length, error);
	if (!root)
		return NULL;
	const JsonValue *tasks = get_json_member(root, "tasks");
	if (!tasks || tasks->kind != JSON_OBJECT) {
		g_set_error(error, ERROR_DOMAIN, 0, "The task file needs a \"tasks\" object");
		return NULL;
	}
	if (!is_missing_or(get_json_member(root, "env"), JSON_OBJECT)) {
		g_set_error(error, ERROR_DOMAIN, 0, "The task file's \"env\" is not an object");
		return NULL;
	}
	for (guint index = 0; index < tasks->names->len; index++) {
		const char *name = g_ptr_array_index(tasks->names, index);
		g_autoptr(GHashTable) visiting = g_hash_table_new(g_str_hash, g_str_equal);
		if (!is_valid_name(name, TRUE)) {
			g_set_error(error, ERROR_DOMAIN, 0, "Task name \"%s\" may only hold letters, digits, - _ and .", name);
			return NULL;
		}
		const char *problem = find_task_problem(get_json_item(tasks, index));
		if (problem) {
			g_set_error(error, ERROR_DOMAIN, 0, "Task \"%s\" %s", name, problem);
			return NULL;
		}
		if (has_call_loop(tasks, name, visiting)) {
			g_set_error(error, ERROR_DOMAIN, 0, "Task \"%s\" ends up running itself", name);
			return NULL;
		}
	}
	return g_steal_pointer(&root);
}


static void free_task_info(TaskInfo *info) {
	g_free(info->name);
	g_free(info->description);
	g_free(info);
}


GPtrArray *read_task_list(const char *project_directory, GError **error) {
	g_autoptr(JsonValue) root = load_task_root(project_directory, error);
	if (!root)
		return NULL;
	const JsonValue *tasks = get_json_member(root, "tasks");
	GPtrArray *list = g_ptr_array_new_with_free_func((GDestroyNotify)free_task_info);
	for (guint index = 0; index < tasks->names->len; index++) {
		TaskInfo *info = g_new0(TaskInfo, 1);
		info->name = g_strdup(g_ptr_array_index(tasks->names, index));
		info->description = g_strdup(get_json_string(get_json_member(get_json_item(tasks, index), "description"),
			NULL));
		g_ptr_array_add(list, info);
	}
	return list;
}


static gboolean is_generated_script(const char *path) {
	g_autofree char *text = NULL;
	gsize length = 0;
	if (!g_file_get_contents(path, &text, &length, NULL))
		return FALSE;
	return g_strstr_len(text, MIN(length, MARKER_SEARCH_BYTES), GENERATED_MARKER) != NULL;
}


/* Removes scripts this program wrote that the task file no longer asks for, leaving files people wrote themselves */
static void remove_stale_scripts(const char *directory, const JsonValue *tasks, const gboolean is_wanted[SCRIPT_KINDS]) {
	g_autoptr(GDir) folder = g_dir_open(directory, 0, NULL);
	const char *entry;
	while (folder && (entry = g_dir_read_name(folder))) {
		for (int kind = 0; kind < SCRIPT_KINDS; kind++) {
			if (!g_str_has_suffix(entry, SCRIPT_EXTENSIONS[kind]))
				continue;
			g_autofree char *name = g_strndup(entry, strlen(entry) - strlen(SCRIPT_EXTENSIONS[kind]));
			g_autofree char *path = g_build_filename(directory, entry, NULL);
			if ((!is_wanted[kind] || !get_json_member(tasks, name)) && is_generated_script(path))
				g_remove(path);
		}
	}
}


gboolean write_task_scripts(const char *project_directory, GError **error) {
	g_autoptr(JsonValue) root = load_task_root(project_directory, error);
	if (!root)
		return FALSE;
	gboolean is_wanted[SCRIPT_KINDS];
	if (!read_wanted_scripts(root, is_wanted, error))
		return FALSE;
	const JsonValue *tasks = get_json_member(root, "tasks");
	g_autofree char *directory = g_build_filename(project_directory, TASK_DIRECTORY, NULL);
	g_autoptr(GPtrArray) paths = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GPtrArray) texts = g_ptr_array_new_with_free_func(g_free);
	/* Each script is made before any is written, so a mistake keeps the previous scripts in place */
	for (guint index = 0; index < tasks->names->len; index++) {
		const char *name = g_ptr_array_index(tasks->names, index);
		for (ScriptKind kind = SCRIPT_SHELL; kind < SCRIPT_KINDS; kind++) {
			if (!is_wanted[kind])
				continue;
			char *text = make_script(kind, root, name, error);
			if (!text)
				return FALSE;
			g_autofree char *file_name = g_strconcat(name, SCRIPT_EXTENSIONS[kind], NULL);
			g_ptr_array_add(paths, g_build_filename(directory, file_name, NULL));
			g_ptr_array_add(texts, text);
		}
	}
	for (guint index = 0; index < texts->len; index++) {
		const char *path = g_ptr_array_index(paths, index);
		if (!g_file_set_contents(path, g_ptr_array_index(texts, index), -1, error))
			return FALSE;
		if (g_str_has_suffix(path, SHELL_EXTENSION))
			g_chmod(path, EXECUTABLE_MODE);
	}
	remove_stale_scripts(directory, tasks, is_wanted);
	return TRUE;
}


gboolean create_task_file(const char *project_directory, GError **error) {
	g_autofree char *path = get_task_file_path(project_directory);
	if (g_file_test(path, G_FILE_TEST_EXISTS))
		return TRUE;
	const char *command = "echo Put your build command here";
	for (guint index = 0; index < G_N_ELEMENTS(BUILD_HINTS); index++) {
		g_autofree char *hint = g_build_filename(project_directory, BUILD_HINTS[index].file, NULL);
		if (g_file_test(hint, G_FILE_TEST_EXISTS)) {
			command = BUILD_HINTS[index].command;
			break;
		}
	}
	g_autoptr(JsonValue) command_value = new_json_string(command);
	g_autofree char *command_json = write_json(command_value);
	g_autofree char *text = g_strdup_printf(
		"// " TASK_HEADER_FIRST "\n"
		"// " TASK_HEADER_SECOND "\n"
		"//\n"
		"// A step is a shell command, or one of these\n"
		"//   {\"run\": {\"unix\": \"make\", \"linux\": \"make\", \"macos\": \"make\", \"windows\": \"nmake\"}}\n"
		"//   {\"copy\": {\"from\": \"assets\", \"to\": \"build/assets\"}}   also \"move\"\n"
		"//   {\"remove\": \"build/tmp\"}   {\"mkdir\": [\"build\", \"dist\"]}\n"
		"//   {\"archive\": {\"from\": \"build/app\", \"to\": \"dist/app.zip\"}}   .zip .tar.gz .tar.xz or .tar\n"
		"//   {\"extract\": {\"from\": \"dist/app.zip\", \"to\": \"unpacked\"}}\n"
		"//   {\"download\": {\"url\": \"https://example.com/file\", \"to\": \"assets/file\"}}\n"
		"//   {\"upload\": {\"from\": \"dist/app.zip\", \"to\": \"user@server:/srv/app/\"}}\n"
		"//   {\"task\": \"other-task\"}   {\"echo\": \"Done\"}\n"
		"//   {\"needs\": [\"cc\", \"make\"]}   {\"pkg\": [\"gtk4 >= 4.14\"]}   stop with a clear message when a tool or library is missing\n"
		"//   {\"each\": {\"from\": \"src/*.c\", \"to\": \"build/{id}.o\", \"run\": \"cc -c {from} -o {to}\", \"also\": \"src/*.h\"}}\n"
		"//   runs for each file that matches, in parallel, and only when the result is older than the file or the extras,\n"
		"//   removes the results of files that are gone, and builds everything again when the command or the\n"
		"//   variables it names change\n"
		"// A task may also have \"description\", \"depends\", \"directory\" and \"env\", and ${NAME} reads a\n"
		"// variable. Paths use / on all systems, and \"systems\": [\"unix\"] beside \"tasks\" writes only the .sh scripts.\n"
		"{\n"
		"\t\"env\": {},\n"
		"\t\"tasks\": {\n"
		"\t\t\"build\": {\n"
		"\t\t\t\"description\": \"Build the project\",\n"
		"\t\t\t\"steps\": [%s]\n"
		"\t\t}\n"
		"\t}\n"
		"}\n", command_json);
	g_autofree char *directory = g_path_get_dirname(path);
	if (g_mkdir_with_parents(directory, FOLDER_MODE) != 0) {
		g_set_error(error, ERROR_DOMAIN, 0, "Could not create %s", directory);
		return FALSE;
	}
	return g_file_set_contents(path, text, -1, error);
}
