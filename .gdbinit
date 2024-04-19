# Add one of these to ~/.config/gdb/gdbinit:
#   add-auto-load-safe-path $HOME/.config/gdb/gdbinit
#   set auto-load safe-path /

set print pretty on

# https://sourceware.org/gdb/current/onlinedocs/gdb.html/Define.html
# https://stackoverflow.com/a/36505832
define skipln
    if $argc == 0
        tbreak +1
        jump +1
    end
    if $argc == 1
        tbreak $arg0
        jump $arg0
    end
end
document skipln
    Usage: skipln [N]

    Skip some lines quickly.

    If no argument, jump to the next line.
    If 1 argument, treat N as relative line number.
end

break vm.c:interpret
break compiler.c:compile
break parser.c:declaration

run
