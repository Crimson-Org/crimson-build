/* Write the shell and Windows scripts of a project's tasks
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

#include "build/tasks.h"

#define PROGRAM_VERSION "0.1.0"

int main(int argument_count, char **arguments) {
	gboolean should_print_version = FALSE;
	const GOptionEntry options[] = {
		{"version", 'v', 0, G_OPTION_ARG_NONE, &should_print_version, "Print the version", NULL},
		G_OPTION_ENTRY_NULL,
	};
	g_autoptr(GOptionContext) context = g_option_context_new("[FOLDER or FILE]");
	g_option_context_set_summary(context,
		"Write a .sh and a .cmd script for each task in .crimson-build/tasks.json.\n"
		"FOLDER is a project folder or its .crimson-build folder, FILE is its tasks.json, and the current folder is the default.");
	g_option_context_add_main_entries(context, options, NULL);
	g_autoptr(GError) error = NULL;
	if (!g_option_context_parse(context, &argument_count, &arguments, &error)) {
		g_printerr("%s\n", error->message);
		return EXIT_FAILURE;
	}
	if (should_print_version) {
		g_print("Crimson Build " PROGRAM_VERSION "\n");
		return EXIT_SUCCESS;
	}
	g_autofree char *project = find_task_project(argument_count > 1 ? arguments[1] : ".", &error);
	if (!project || !write_task_scripts(project, &error)) {
		g_printerr("%s\n", error->message);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
