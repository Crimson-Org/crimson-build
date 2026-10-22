#pragma once

#include <glib.h>

/* The folder in a project that keeps the task file and the scripts made from it */
#define TASK_DIRECTORY ".crimson-build"
#define TASK_FILE_NAME "tasks.json"

/* A task as the task menu lists it */
typedef struct {
	char *name, *description;
} TaskInfo;

char *get_task_file_path(const char *project_directory);
/* Finds the project from a project folder, its task folder or its task file, and nothing when the path is none of them */
char *find_task_project(const char *path, GError **error);
/* Writes a small starting task file that lists each step kind, when the project has none */
gboolean create_task_file(const char *project_directory, GError **error);
/* Lists the tasks in written order, or NULL with an error when the file is missing or broken */
GPtrArray *read_task_list(const char *project_directory, GError **error);
/* Writes a POSIX shell script and a Windows command script for each task and removes stale ones */
gboolean write_task_scripts(const char *project_directory, GError **error);
/* The shell command that runs a task's script from any directory */
char *get_task_command(const char *project_directory, const char *name);
