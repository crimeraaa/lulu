# Add one of these to ~/.config/gdb/gdbinit:
#   add-auto-load-safe-path $HOME/.config/gdb/gdbinit
#   set auto-load safe-path /


set print pretty on

# Ensure we can do `import printers`
python
import os
import sys
if os.getcwd() not in sys.path:
    sys.path.insert(0, os.getcwd())
end

# file ./bin/lua

# # Protected call of `luaD_protectedparser()`
# break ldo.c:f_parser
# break luaY_parser
# break lua_pcall
# break luaD_precall if nresults == -1
# break luaV_execute

# Test error call stack
# break luaG_aritherror
# break luaG_typeerror

file ./bin/lulu

break lulu.c:main

# break lulu_main::main
# break lulu::run
# break lulu::[compiler.odin]::compiler_compile
# break lulu::[parser.odin]::parser_program

layout src

run

# layout src by default focuses on the source code window; this makes arrow keys
# navigate that rather than the command-line.
focus cmd
