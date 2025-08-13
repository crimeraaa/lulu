# :moon: Lulu

**Lulu** is a Lua 5.1 interpreter written in the Odin programming language and
in C++,
heavily inspired by [Crafting Interpreters](https://craftinginterpreters.com/)
by Robert Nystrom [(munificent)](https://github.com/munificent/craftinginterpreters).

This is mainly a hobby project with an emphasis on developing my understanding.
Although there are parts concerned with performance, my main goal is documenting the process and insights.

# Building

## Requirements

1. [Task](https://taskfile.dev/)

### lulu-odin

1. [Odin](https://odin-lang.org/)
    - This, in turn, requires Clang 14 or 18.

### lulu-cpp

1. C++ compiler
    - `g++` or `clang++` work.
    - MSVC builds still a work in progress!
    - Must support the C++17 standard due to the frequent use of `constexpr`
    variables and functions.

2. C compiler
    - `gcc` or `clang` work.
    - Used only for release builds.
    - Needs to only support the C89 standard. Although the shared object may link
    to the target's C++ standard library, the API can be consumed by a pure C
    application.

## Overview
### Building

```
task <dir>:build [MODE=mode]
```

Builds the sub-project executable (and shared library, if applicable) if needed.
All sub-projects implement this task.

`dir` :
Required parameter, one of `cpp`, `odin` or `lua`.
Represents the directory of the sub-project to build.


`MODE` :
Optional named parameter. Indicates the optimization level.

`mode` :
Value to be passed to `MODE`. One of `debug` (default) or `release`.

Examples:
```
task lua:build
task odin:build MODE=debug
task cpp:build MODE=release
```

### Running

- Format: `task <dir>:run [MODE=mode]`

Runs the sub-project executable, re-building it if needed.
Parameters are the exact same as [building](#building-1).
All sub-projects implement this task.

Examples:
```
task lua:run
task odin:run MODE=debug
task cpp:run MODE=release
```

# Configuration

The following are sub-project-specific configuration options. In order to change
them, you must modify their respective Taskfiles directly.

## lulu-odin

```
odin/Taskfile.yml
```

The following options are passed directly to the Odin compiler.
They are command-line arguments (to `odin`, not `task`) in the form
`-define:NAME=value`.

`value` can only be a boolean, number or string literal.

1. `LULU_USE_READLINE`
    -   A boolean indicating whether or not to use the GNU Readline
        Library for user input.
    -   If not provided, defaults to `true` if we are building on Linux,
        else `false`.
1. `LULU_DEBUG`
    -   Boolean value. Indicates if we are in debug mode or not
1. `LULU_DEBUG_PRINT_CODE`
    -   Boolean value. Indicates if we should dump disassembly of chunks when
        we finish compiling. Useful to check correctness of
        bytecode.
1. `LULU_DEBUG_TRACE_EXEC`
    -   Boolean value. Indicates if we should print disassembly inline with
        each instruction that has been decoded. Useful to check correctness
        of bytecode and execution implementation thereof.
1. `PROMPT`
    -   A string literal used in the interactive mode in `lulu.odin`.
    -   Defaults to `">>> "` similar to Python.

## lulu-cpp

```
cpp/Taskfile.yml
```

The following options are passed directly to the C++ compiler.
They are command-line arguments (to `g++` or `clang++`, not `task`) in the form
`-DNAME=value`.

1. `LULU_DEBUG`
    - Indicates whether we are compiling in debug mode or note.
    - 0 or undefined indicates release mode. 1 indicates debug mode.

1. `LULU_DEBUG_PRINT_CODE`
    - Indicates whether to pretty-print Lua chunks when done compiling.
    - This is useful to check if the resulting bytecode is correct. However,
    printing each chunk can take up a lot of space on the standard output.

1. `LULU_DEBUG_TRACE_EXEC`
    - Indicates whether to print disassembly inline with execution.
    - This is useful to check if execution is correct. However, since it also
    prints the contents of the current Lua stack frame, it can take up a lot of
    space on the standard output.

# Note

This is just a fun little project where I retrace my steps from the Lox
interpreter and remake them in the context of a Lua interpreter.

I'm mainly interested in reducing global state, learning about optimizations and
supporting some grammar/semantics that Lua provides.

This is **NOT** supposed to be a 1-for-1 copy of the official C source code for Lua.
If I just copied it, where's the fun in that?

## The Lua 5.1.5 Source

I've included a heavily modified copy of the Lua 5.1.5 C source code.
It is mainly for documenting my understanding of the many different
moving parts. I avoid modifying the core logic if at all possible,
however.

The main significant differences are:

1. Naming
    -   e.g. `expdesc` is renamed `Expr`.
    -   The `expkind` enum is now renamed to `Expr_Type`.
    -   `V*` enums are renamed to the form `EXPR_*`.
    -   Fields of various structs are renamed, e.g. the `ls` member in
        `lparser.h:FuncState` was renamed to `lex` to better reflect
        what it even means.
    -   Likewise, `p` in `lparser.h:FuncState` was renamed to `proto`.
    -   In general I heavily dislike single-letter variable namees for
        struct members as they are often far-removed from their
        declaration that their type is not immediately obvious.

1. Code Style
    -   Whenever possible, I opted to "expand" very terse expressions.
    -   E.g. in `ldo.c:f_parser()` there is a ternary expression that
        returns eithers `luaU_undump` or `luaY_parser`, *then* calls
        it with the appropriate arguments.
    -   I rewrote the ternary expression into a plain `if`-`else`
        statement for clarity.
    -   Rewriting such code in general is risky because you run the risk
        of misunderstanding it. Moreso in C, where terse constructs like
        `*ptr++` are common.
    -   It is VERY important to understand exactly what an expression is
        doing and what side-effects it may cause before even considering
        rewriting it.
    -   Single-line (i.e. no curly braces)`if`,`else`,`for`,`while`,
        and other such statements have had curly braces added. This is
        entirely personal preference.

1. Comments
    -   Many comments have been added to help document what is being
        done in some places and why.
    -   e.g. why does `FuncState` have an "active variables" array and
        `Proto` have a "local variables" array?
    -   `Proto::locvar[]` is an array of every possible local variable,
        in order of declaration, for that particular function.
    -   `FuncState::actvar[]` simply refers to which indexes into
        `FuncState::proto::locvar[]` are to be used.
    -   By pushing and popping to `actvar`, we can effectively tell
        which locals are currently in scope and not.
