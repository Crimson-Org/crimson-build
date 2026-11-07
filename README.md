<h1 align="center">Crimson Build</h1>
<p align="center">A command line tool and C library that turns Crimson <code>tasks.json</code> files into shell and Windows scripts that run anywhere.</p>

<p align="center">
  <a href="https://github.com/Crimson-Org/crimson-build/releases/latest"><b>Download Here!</b></a>
</p>

## Why

Crimson Build writes plain scripts, so building a project needs only a shell, a compiler and the project's libraries.\
The scripts work without Crimson Build installed.

## About

### A command

`crimson-build` reads a project's `tasks.json` and writes a `.sh` script for Linux and macOS and a `.cmd` script for Windows into `.crimson-build/`.\
`crimson-build FOLDER` does the same for another folder.

### A library

Programs can use the same code to read tasks, list them and write the scripts.\
Add the repository as a submodule in `subprojects/crimson-build` and compile `src/build/*.c` with `-Isubprojects/crimson-build/src`.

### The format

A task is a list of steps, and `${NAME}` reads a variable.\
Steps run shell commands, copy, move, remove, archive, extract, download or upload files, call other tasks, check that tools and libraries are installed, and build only the files that changed.\
`"systems": ["unix"]` writes only the shell scripts, for a project that builds only on Linux and macOS.

## Example

This `.crimson-build/tasks.json` builds every `src/*.c` into a program and runs the program.

```json
{
	"env": {"PROGRAM": "hello"},
	"tasks": {
		"build": {
			"description": "Build hello",
			"steps": [
				{"needs": ["cc"]},
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

`crimson-build` writes `build.sh`, `build.cmd`, `run.sh` and `run.cmd` next to `tasks.json`.\
`sh .crimson-build/run.sh` builds only the files that changed and starts the program.

## Building

You need a C compiler, pkg-config and the GLib development files.

Steps:
 1. Clone using `git clone https://github.com/Crimson-Org/crimson-build`
 2. Run `sh .crimson-build/install.sh`

`sh .crimson-build/uninstall.sh` uninstalls Crimson Build.
