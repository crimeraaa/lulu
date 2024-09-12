# Add one of these to ~/.config/gdb/gdbinit:
#   add-auto-load-safe-path $HOME/.config/gdb/gdbinit
#   set auto-load safe-path /

file ./bin/lulu
set print pretty on

break lulu.c:main
break vm.c:compile_and_run
break vm.c:compile_only

# break api.c:lulu_load
# break lexer.c:number_token
# break string.c:luluStr_concat
layout src

run
