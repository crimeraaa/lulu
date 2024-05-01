# Add one of these to ~/.config/gdb/gdbinit:
#   add-auto-load-safe-path $HOME/.config/gdb/gdbinit
#   set auto-load safe-path /

file ./bin/lulu
set print pretty on

break vm.c:interpret
break compiler.c:compile
break parser.c:declaration
# break parser.c:ident_statement
break parser.c:table
layout src

run
