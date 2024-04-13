# Add one of these to ~/.config/gdb/gdbinit:
#   add-auto-load-safe-path $HOME/.config/gdb/gdbinit
#   set auto-load safe-path /
# Note that I'm using https://github.com/cyrus-and/gdb-dashboard

# Disable all the following modules
dashboard assembly
dashboard registers
dashboard threads
dashboard memory
dashboard stack
dashboard breakpoints
# dashboard history

# Use Python-style booleans.
# Height of 0 will take up the entire pane.
dashboard source -style height 30
dashboard source -style highlight-line True
dashboard variables -style compact False

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
tbreak compiler.c:expression

run
