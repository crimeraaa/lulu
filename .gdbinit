# Add one of these to ~/.config/gdb/gdbinit:
#   add-auto-load-safe-path $HOME/.config/gdb/gdbinit
#   set auto-load safe-path /


file ./bin/lua
set print pretty on
source printers/lua.py

break lua.c:main
# Protected call of `luaD_protectedparser()`
break ldo.c:f_parser
break luaY_parser
break lua_pcall
break luaD_call if nResults == -1
break luaV_execute

# file ./bin/lulu
# break lulu::main
# break lulu::[compiler.odin]::compiler_emit_not


layout src

run
