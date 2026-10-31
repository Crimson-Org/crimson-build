<h1 align="center">Crimson Build</h1>
<p align="center">A command and a library that turn the Crimson <code>tasks.json</code> format into shell and Windows scripts that run anywhere.</p>

<p align="center">
  <a href="https://github.com/Crimson-Org/crimson-build/releases/latest"><b>Download Here!</b></a>
</p>

## Why

Building most projects means installing the tool that builds them first.\
Crimson Build writes the build as plain scripts instead, so a shell, a compiler and the project's libraries are all anyone needs.

## About

### A command

`crimson-build` reads the `tasks.json` in a project and writes a `.sh` script for Linux and macOS and a `.cmd` script for Windows for each task into `.crimson-build/`.\
`crimson-build FOLDER` does it for another project, and the current folder is the default.

### A library

Any program can import the same code from `src/build` to read tasks, list them and write the scripts itself, which is how Crimson Code builds its task menu.\
Add the repository as a submodule in `subprojects/crimson-build` and compile `src/build/*.c` with `-Isubprojects/crimson-build/src`.

### Its own format

`tasks.json` is Crimson's own format and not a general one.\
A task is a list of steps and `${NAME}` reads a variable.\
Steps run shell commands, copy, move, remove, archive, extract, download or upload files, call another task, check that tools and libraries are installed, and build only the files that changed.

### Scripts that stand alone

Nobody needs Crimson Build to run the scripts, only a shell.

## Example

This `.crimson-build/tasks.json` builds every `src/*.c` into a program and runs it.

```json
{
	"env": {"PROGRAM": "hello"},
	"tasks": {
		"build": {
			"description": "Build hello",
			"steps": [
				{"needs": ["cc", "make"]},
				{"each": {"from": "src/*.c", "to": "build/{id}.o", "run": "cc -c {from} -o {to}"}},
				"cc -o ${PROGRAM} build/*.o"
			]
		},
		"run": {
			"description": "Build and start hello",
			"depends": "build",
			"steps": ["./${PROGRAM}"]
		}
	}
}
```

`crimson-build` writes `build.sh`, `build.cmd`, `run.sh` and `run.cmd` next to it.\
`sh .crimson-build/run.sh` builds only the files that changed and starts the program.

## Building

You need a C compiler, make, pkg-config and the GLib development files.

Steps:
 1. Clone using `git clone https://github.com/Crimson-Org/crimson-build`
 2. Run `sh .crimson-build/install.sh`

`sh .crimson-build/uninstall.sh` removes it again.
