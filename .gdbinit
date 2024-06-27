# Add one of these to ~/.config/gdb/gdbinit:
#   add-auto-load-safe-path $HOME/.config/gdb/gdbinit
#   set auto-load safe-path /

file ./bin/lulu
set print pretty on

break api.c:lulu_interpret
break lexer.c:number_token
layout src

run
