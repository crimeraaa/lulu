# :moon: Lulu

**Lulu** is a Lua 5.1 interpreter written in the Odin programming language,
heavily inspired by [Crafting Interpreters](https://craftinginterpreters.com/)
by Robert Nystrom [(munificent)](https://github.com/munificent/craftinginterpreters).

This is mainly a hobby project with an emphasis on developing my understanding.
Although there are parts concerned with performance, my main goal is documenting the process and insights.

# Building

## Requirements

1. Odin
    - In turn requires Clang 14/18.
2. [Task](https://taskfile.dev/)
    - `task build [MODE=mode]` to build.
        - `MODE` is an optional parameter.
        - `mode` can be one of `debug` (default) or `release`.
    - `task run` to build if needed then run in interactive mode.

# Configuration

The following options are passed directly to the Odin compiler.
They are command-line arguments in the form `-define:NAME=value`.

`value` can only be a boolean, number or string literal.

1. `USE_READLINE`
    -   A boolean indicating whether or not to use the GNU Readline
        Library for user input.
    -   If not provided, defaults to `true` if we are building on Linux,
        else `false`.
2. `PROMPT`
    -   A string literal used in the interactive mode in `lulu.odin`.
    -   Defaults to `">>> "` similar to Python.

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
    -   The `expkind` enum is now renamed to `ExprKind`.
    -   `V*` enums are renamed to the form `Expr_*`.
    -   Fields of various structs are renamed, e.g. the `ls` member in
        `lparser.h:FuncState` was renamed to `lex` to better reflect
        what it even means.
    -   Likewise, `p` in `lparser.h:FuncState` was renamed to `proto`.
    -   In general I heavily dislike single-letter variable namees for
        struct members as they are often far-removed from their
        declaration that their type is not immediately obvious.

2. Code Style
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

3. Comments
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
