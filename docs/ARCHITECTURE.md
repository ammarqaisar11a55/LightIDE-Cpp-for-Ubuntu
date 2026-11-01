# LightIDE Architecture

LightIDE is a small native C/C++ IDE built on Qt 6 Widgets and QScintilla. It
keeps the Dev-C++ workflow: open a `.cpp`, press F11, and type into the running
program. Projects, the debugger and settings exist, but none of them stand
between the user and that first run.

## Environment

| | Target | Development machine |
|---|---|---|
| OS | Ubuntu 24.04 | Ubuntu 26.04 |
| Qt | 6.4 | 6.10 |
| GCC | 13 | 15.2 |
| GDB | 15 | 17.1 |
| QScintilla | 2.14 | 2.14 |

The code keeps to APIs available in Qt 6.4 so that it builds on the target.
Development happens on the newer stack, so see "Known limitations" below.

## Layers

```text
                 +-----------------------------------+
   lightide      |  ui/       MainWindow, tabs,      |  Qt Widgets
   (executable)  |            panels, dialogs        |
                 |  editor/   CodeEditor, lexer      |  QScintilla
                 +-----------------+-----------------+
                                   | signals/slots only
                 +-----------------v-----------------+
   lightide_core |  compiler/  detection, commands,  |  Qt Core only:
   (static lib)  |             builds, diagnostics   |  no widgets, so
                 |  execution/ PTY process runner,   |  every piece is
                 |             terminal filter       |  unit-testable
                 |  debugger/  GDB/MI client         |  without a
                 |  project/   project model         |  display
                 |  filesystem/ file I/O             |
                 |  core/      settings              |
                 +-----------------+-----------------+
                                   | fork/exec, QProcess
                 +-----------------v-----------------+
                 |  g++, gcc, gdb, the user's binary |  separate processes
                 +-----------------------------------+
```

The split into two targets is what enforces "UI separate from business logic":
`lightide_core` does not link Qt Widgets, so it cannot depend on a widget by
accident.

## Key decisions

### Programs run on a pseudo-terminal, not on QProcess pipes

With pipes, a C program's stdout is fully buffered. `printf("Name: ");
scanf(...)` would show nothing until the buffer fills or the program exits,
which breaks exactly the interactive programs this IDE exists for. `isatty()`
would also be false.

`ProcessRunner` therefore uses `forkpty()`. The child gets a real controlling
terminal, in its own session, so:

- prompts appear immediately, and Ctrl+C and Ctrl+D behave as in a terminal;
- line editing and echo come from the kernel's line discipline, and the output
  view only forwards keystrokes and renders what comes back;
- Stop signals the whole process group, so children of the program die too.

stdout and stderr share the terminal. That keeps them in the order they were
written, which two separate streams cannot guarantee, at the cost of not
colouring stderr differently.

Output is read in bounded chunks with back-pressure: once the view is behind,
the runner stops reading and the kernel blocks the program. A
`while (1) cout << "x\n";` therefore cannot flood the GUI thread, and Stop stays
responsive.

### Asynchronous everything

Compiler detection, builds, program runs and GDB are all event-driven on the GUI
thread through `QProcess` and `QSocketNotifier`. No `waitFor*` calls run on the
GUI thread, so no worker threads are needed for these. Find in Files is the one
CPU-bound task and runs on a `QThread`.

### Diagnostics

Compilers run with `LC_ALL=C`, `-fdiagnostics-color=never` and
`-fmessage-length=0`, which gives untranslated keywords and one diagnostic per
line. The parser anchors on `:<line>:<col>: <severity>:` and takes everything
before it as the path, which is what makes paths with spaces work.

### Single-file builds and project builds share one pipeline

`BuildProcess` runs a list of compile jobs in parallel followed by an optional
link job. A single file is one job. A project is one job per stale source,
decided from timestamps and `-MMD -MP` dependency files, followed by a link.

### GDB/MI

`DebuggerManager` talks to `gdb --interpreter=mi3` through a hand-written MI
parser. The inferior gets its own PTY through `-inferior-tty-set`, and that PTY
feeds the same Application Output view as a normal run. Debuginfod is turned off
so that a debug session never touches the network.

### Project format

Projects are `.lideproj` files holding plain indented JSON with source paths
relative to the project file, so they can be read, diffed and edited by hand.

### Icons

Icons are small SVG files with `currentColor` strokes, tinted at runtime for the
active theme. One icon set therefore serves both themes.

## Windows path

POSIX-specific code is confined to `execution/`: `ProcessRunner` and the `Pty`
helper. Everything else is Qt. A Windows port needs a ConPTY backend for those
two classes, and the `sudo apt` hint in the missing-compiler message replaced.

## Known limitations

- Developed and tested on Ubuntu 26.04 with Qt 6.10 and GCC 15. The Qt 6.4 and
  GCC 13 combination on 24.04 is targeted by avoiding newer APIs, but it needs
  verifying on a 24.04 machine.
- The output view is a line-oriented terminal, not a full emulator. It handles
  backspace, carriage return and strips ANSI escapes. Full-screen programs such
  as `vim` or `ncurses` applications will not render.
- Lines typed into a running program are limited to 4095 bytes by the kernel's
  canonical-mode line buffer.
- Go to Definition and completion are text-based. There is no semantic index,
  which is a deliberate trade to keep the IDE light.

## Roadmap

1. Foundation: CMake, main window, menus, toolbar, tabs, file operations.
2. Code editor: QScintilla, highlighting, indentation, folding.
3. Compiler: detection, builds, diagnostics, settings.
4. Execution: PTY runner, interactive input, stop and cleanup.
5. Debugger: MI parser, breakpoints, stepping, locals, call stack.
6. Projects: format, explorer, incremental multi-file builds.
7. Polish: themes, settings dialog, tests, packaging, README.
