#include <string.h>

#include <glib/gstdio.h>

#include "build/json.h"
#include "build/tasks.h"

#define GENERATED_MARKER "Generated from " TASK_FILE_NAME " by Crimson Build"
#define MARKER_SEARCH_BYTES 400
#define SHELL_EXTENSION ".sh"
#define WINDOWS_EXTENSION ".cmd"
#define EXECUTABLE_MODE 0755
#define ERROR_DOMAIN g_quark_from_static_string("crimson-code-tasks")

/* The two kinds of script made for each task */
typedef enum {
	SCRIPT_SHELL,
	SCRIPT_WINDOWS,
} ScriptKind;

/* A script being written, with the helper functions its steps call */
typedef struct {
	ScriptKind kind;
	GString *steps;
	guint used_helpers;
	/* Each task name, so calls to other tasks can be checked */
	const JsonValue *tasks;
} Script;

/* Steps that call a small helper function in each script, which keeps the step lines readable */
typedef struct {
	const char *name;
	/* Member names of the two arguments, or NULL for a step taking one path or a list of paths */
	const char *fields[2];
	gboolean are_paths[2];
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
		"\tpkg-config --exists \"$1\" || { printf '%s\\n' \"The development files for $1 are missing, install them with your package manager\" >&2; return 1; }\n"
		"}\n",
		":crimson_pkg\n"
		"pkg-config --exists \"%~1\" || (echo The development files for %~1 are missing & exit /b 1)\n"
		"exit /b 0\n"},
	{"each", {NULL, NULL}, {FALSE, FALSE},
		"crimson_each() {\n"
		"\tcrimson_rules=$(mktemp) || return 1\n"
		"\tcrimson_make=$(mktemp) || return 1\n"
		"\tcrimson_extra=\n"
		"\tfor crimson_glob in $4; do\n"
		"\t\tfor crimson_file in $crimson_glob; do\n"
		"\t\t\t[ -e \"$crimson_file\" ] && crimson_extra=\"$crimson_extra $crimson_file\"\n"
		"\t\tdone\n"
		"\tdone\n"
		"\tcrimson_all=\n"
		"\tcrimson_sep=$(printf '\\037')\n"
		"\tfor crimson_from in $1; do\n"
		"\t\t[ -e \"$crimson_from\" ] || continue\n"
		"\t\tcrimson_id=$(printf '%s' \"${crimson_from%.*}\" | tr '/.' '__')\n"
		"\t\tcrimson_name=$(basename \"${crimson_from%.*}\")\n"
		"\t\tcrimson_to=$(printf '%s' \"$2\" | sed -e \"s${crimson_sep}{id}${crimson_sep}$crimson_id${crimson_sep}g\" -e \"s${crimson_sep}{name}${crimson_sep}$crimson_name${crimson_sep}g\")\n"
		"\t\tcrimson_command=$(printf '%s' \"$3\" | sed -e 's/\\$/$$/g' -e \"s${crimson_sep}{from}${crimson_sep}$crimson_from${crimson_sep}g\" -e \"s${crimson_sep}{to}${crimson_sep}$crimson_to${crimson_sep}g\")\n"
		"\t\tcrimson_all=\"$crimson_all $crimson_to\"\n"
		"\t\tprintf '%s: %s%s\\n\\t@mkdir -p \"$(dir $@)\"\\n\\t@echo \"==> %s\"\\n\\t%s\\n\\n' \"$crimson_to\" \"$crimson_from\" \"$crimson_extra\" \"$crimson_from\" \"$crimson_command\" >> \"$crimson_rules\"\n"
		"\tdone\n"
		"\t{ printf 'all:%s\\n\\n' \"$crimson_all\"; cat \"$crimson_rules\"; } > \"$crimson_make\"\n"
		"\tcrimson_status=0\n"
		"\tmake -s -f \"$crimson_make\" -j\"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)\" all || crimson_status=$?\n"
		"\trm -f \"$crimson_make\" \"$crimson_rules\"\n"
		"\treturn \"$crimson_status\"\n"
		"}\n",
		":crimson_each\n"
		"echo Building each file needs a Unix shell with make\n"
		"exit /b 1\n"},
};

/* Files that tell which build command a new project most likely uses */
static const struct {
	const char *file, *command;
} BUILD_HINTS[] = {
	{"build.sh", "./build.sh --build-only"}, {"meson.build", "meson compile -C build"},
	{"Cargo.toml", "cargo build"}, {"CMakeLists.txt", "cmake --build build"}, {"Makefile", "make"},
	{"go.mod", "go build ./..."}, {"package.json", "npm run build"}, {"build.zig", "zig build"},
};


/* ==================================== Text conversion ==================================== */


static gboolean is_variable_start(const char *text) {
	return text[0] == '$' && text[1] == '{';
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


/* Rewrites ${NAME} as %NAME%, with paths turned to backslashes and literal percent signs doubled */
static void append_windows_text(GString *output, const char *text, gboolean is_path, gboolean escapes_percent) {
	for (const char *character = text; *character; character++) {
		const char *close = is_variable_start(character) ? strchr(character, '}') : NULL;
		if (close) {
			g_string_append_c(output, '%');
			g_string_append_len(output, character + 2, close - character - 2);
			g_string_append_c(output, '%');
			character = close;
		} else if (*character == '/' && is_path) {
			g_string_append_c(output, '\\');
		} else if (*character == '%' && escapes_percent) {
			g_string_append(output, "%%");
		} else if (*character != '"') {
			g_string_append_c(output, *character);
		}
	}
}


static void append_windows_quoted(GString *output, const char *text, gboolean is_path) {
	g_string_append_c(output, '"');
	append_windows_text(output, text, is_path, TRUE);
	g_string_append_c(output, '"');
}


/* Writes text that echo prints as is apart from variables, since cmd treats these characters as commands */
static void append_windows_echo(GString *output, const char *text) {
	g_string_append(output, "echo(");
	for (const char *character = text; *character; character++) {
		const char *close = is_variable_start(character) ? strchr(character, '}') : NULL;
		if (close) {
			g_string_append_printf(output, "%%%.*s%%", (int)(close - character - 2), character + 2);
			character = close;
			continue;
		}
		if (strchr("^&|<>()", *character))
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


static void append_label(Script *script, const char *label) {
	if (script->kind == SCRIPT_SHELL) {
		g_string_append(script->steps, "printf '==> %s\\n' ");
		append_single_quoted(script->steps, label);
		g_string_append_c(script->steps, '\n');
	} else {
		g_autofree char *line = g_strconcat("==> ", label, NULL);
		append_windows_echo(script->steps, line);
	}
}


/* Stops a Windows script when the step before failed, which the shell script gets from set -e */
static void append_failure_check(Script *script) {
	if (script->kind == SCRIPT_WINDOWS)
		g_string_append(script->steps, "if errorlevel 1 exit /b %errorlevel%\n");
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
	append_windows_text(script->steps, command, FALSE, FALSE);
	g_string_append_c(script->steps, '\n');
	append_failure_check(script);
	return TRUE;
}


static gboolean write_task_step(Script *script, const JsonValue *argument, GError **error) {
	const char *name = get_json_string(argument, NULL);
	if (!name || !get_json_member(script->tasks, name)) {
		g_set_error(error, ERROR_DOMAIN, 0, "A task step names a task that does not exist");
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
		append_windows_echo(script->steps, text);
	}
	return TRUE;
}


static void append_helper_call(Script *script, const FileStep *step, const char *const *values) {
	g_autofree char *label = values[1] ? g_strdup_printf("%s %s to %s", step->name, values[0], values[1])
		: g_strdup_printf("%s %s", step->name, values[0]);
	append_label(script, label);
	g_string_append_printf(script->steps, script->kind == SCRIPT_SHELL ? "crimson_%s" : "call :crimson_%s", step->name);
	for (int index = 0; index < 2 && values[index]; index++) {
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
		for (guint index = 0; index < count; index++) {
			const char *values[2] = {get_json_string(is_list ? get_json_item(argument, index) : argument, NULL)};
			if (!values[0]) {
				g_set_error(error, ERROR_DOMAIN, 0, "A %s step takes a path or a list of paths", step->name);
				return FALSE;
			}
			append_helper_call(script, step, values);
		}
		return TRUE;
	}
	const char *values[2] = {
		get_json_string(get_json_member(argument, step->fields[0]), NULL),
		get_json_string(get_json_member(argument, step->fields[1]), NULL),
	};
	if (!values[0] || !values[1]) {
		g_set_error(error, ERROR_DOMAIN, 0, "A %s step takes \"%s\" and \"%s\"", step->name, step->fields[0],
			step->fields[1]);
		return FALSE;
	}
	append_helper_call(script, step, values);
	return TRUE;
}


/* Compiles or converts every file that matches a pattern, only those whose result is older than the file or its extras */
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
	guint count = also && also->kind == JSON_ARRAY ? get_json_length(also) : (also ? 1 : 0);
	for (guint index = 0; index < count; index++) {
		const char *pattern = get_json_string(also->kind == JSON_ARRAY ? get_json_item(also, index) : also, NULL);
		if (!pattern) {
			g_set_error(error, ERROR_DOMAIN, 0, "The \"also\" of an each step takes patterns");
			return FALSE;
		}
		g_string_append_printf(extras, "%s%s", extras->len ? " " : "", pattern);
	}
	for (guint index = 0; index < G_N_ELEMENTS(FILE_STEPS); index++)
		if (strcmp(FILE_STEPS[index].name, "each") == 0)
			script->used_helpers |= 1u << index;
	g_autofree char *label = g_strdup_printf("each %s", from);
	append_label(script, label);
	if (script->kind == SCRIPT_WINDOWS) {
		g_string_append(script->steps, "call :crimson_each\n");
		append_failure_check(script);
		return TRUE;
	}
	g_string_append(script->steps, "crimson_each ");
	const char *values[4] = {from, to, run, extras->str};
	for (int index = 0; index < 4; index++) {
		append_shell_quoted(script->steps, values[index]);
		g_string_append_c(script->steps, index < 3 ? ' ' : '\n');
	}
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
			g_string_append_printf(script->steps, "set \"%s=", name);
			append_windows_text(script->steps, value, FALSE, TRUE);
			g_string_append(script->steps, "\"\n");
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
	return TRUE;
}


static const JsonValue *get_task_steps(const JsonValue *task) {
	if (task->kind == JSON_OBJECT)
		return get_json_member(task, "steps");
	return task;
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
	g_string_append_printf(text, "\n%s", script->steps->str);
	if (script->kind == SCRIPT_WINDOWS) {
		g_string_append(text, "exit /b 0\n");
		for (guint index = 0; index < G_N_ELEMENTS(FILE_STEPS); index++)
			if (script->used_helpers & (1u << index))
				g_string_append_printf(text, "\n%s", FILE_STEPS[index].windows_helper);
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
	Script script = {kind, g_string_new(NULL), 0, tasks};
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
	for (guint index = 0; index < tasks->names->len; index++) {
		const char *name = g_ptr_array_index(tasks->names, index);
		g_autoptr(GHashTable) visiting = g_hash_table_new(g_str_hash, g_str_equal);
		if (!is_valid_name(name, TRUE)) {
			g_set_error(error, ERROR_DOMAIN, 0, "Task name \"%s\" may only hold letters, digits, - _ and .", name);
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


/* Removes scripts this program wrote for tasks that are gone, leaving files people wrote themselves */
static void remove_stale_scripts(const char *directory, const JsonValue *tasks) {
	g_autoptr(GDir) folder = g_dir_open(directory, 0, NULL);
	const char *entry;
	while (folder && (entry = g_dir_read_name(folder))) {
		const char *extension = g_str_has_suffix(entry, SHELL_EXTENSION) ? SHELL_EXTENSION
			: g_str_has_suffix(entry, WINDOWS_EXTENSION) ? WINDOWS_EXTENSION : NULL;
		if (!extension)
			continue;
		g_autofree char *name = g_strndup(entry, strlen(entry) - strlen(extension));
		g_autofree char *path = g_build_filename(directory, entry, NULL);
		if (!get_json_member(tasks, name) && is_generated_script(path))
			g_remove(path);
	}
}


gboolean write_task_scripts(const char *project_directory, GError **error) {
	g_autoptr(JsonValue) root = load_task_root(project_directory, error);
	if (!root)
		return FALSE;
	const JsonValue *tasks = get_json_member(root, "tasks");
	g_autofree char *directory = g_build_filename(project_directory, TASK_DIRECTORY, NULL);
	g_autoptr(GPtrArray) texts = g_ptr_array_new_with_free_func(g_free);
	/* Each script is made before any is written, so a mistake keeps the previous scripts in place */
	for (guint index = 0; index < tasks->names->len; index++) {
		for (ScriptKind kind = SCRIPT_SHELL; kind <= SCRIPT_WINDOWS; kind++) {
			char *text = make_script(kind, root, g_ptr_array_index(tasks->names, index), error);
			if (!text)
				return FALSE;
			g_ptr_array_add(texts, text);
		}
	}
	for (guint index = 0; index < texts->len; index++) {
		const char *name = g_ptr_array_index(tasks->names, index / 2);
		gboolean is_shell = index % 2 == 0;
		g_autofree char *file_name = g_strconcat(name, is_shell ? SHELL_EXTENSION : WINDOWS_EXTENSION, NULL);
		g_autofree char *path = g_build_filename(directory, file_name, NULL);
		if (!g_file_set_contents(path, g_ptr_array_index(texts, index), -1, error))
			return FALSE;
		if (is_shell)
			g_chmod(path, EXECUTABLE_MODE);
	}
	remove_stale_scripts(directory, tasks);
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
		"// Saving this file writes each task as a .sh script for Linux and macOS and a .cmd script for\n"
		"// Windows in this folder, and those scripts run anywhere without Crimson Build.\n"
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
		"//   runs for each file that matches, in parallel, and only when the result is older than the file or the extras\n"
		"// A task may also have \"description\", \"depends\", \"directory\" and \"env\", and ${NAME} reads a\n"
		"// variable. Paths use / on all systems.\n"
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
	if (g_mkdir_with_parents(directory, 0755) != 0) {
		g_set_error(error, ERROR_DOMAIN, 0, "Could not create %s", directory);
		return FALSE;
	}
	return g_file_set_contents(path, text, -1, error);
}
