# :moon: Lulu

**Lulu** is a Lua interpreter written in C, relying only on the C standard library,
heavily inspired by [Crafting Interpreters](https://craftinginterpreters.com/) by
Robert Nystrom [(munificent)](https://github.com/munificent/craftinginterpreters).

# Note

This is just a fun little project where I retrace my steps from the Lox interpreter
and remake them in the context of a Lua interpreter.

I'm mainly interested in reducing global state, learning about optimizations and
supporting some grammar/semantics that Lua provides.

This is **NOT** supposed to be a 1-for-1 copy of the official C source code for Lua.
If I just copied it, where's the fun in that?
