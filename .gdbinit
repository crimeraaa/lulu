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
# Parent of `luaD_precall` call we care about
break luaD_call if nResults == -1
break luaV_execute

# Test error call stack
break luaG_aritherror
break luaG_typeerror

# file ./bin/lulu
# break lulu::main
# break lulu::[compiler.odin]::compiler_emit_not


layout src

run
