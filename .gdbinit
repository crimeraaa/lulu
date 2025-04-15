# Add one of these to ~/.config/gdb/gdbinit:
#   add-auto-load-safe-path $HOME/.config/gdb/gdbinit
#   set auto-load safe-path /

file ./bin/lulu
set print pretty on

break lulu::main
# break lulu::[compiler.odin]::compiler_emit_not

layout src

run
