#!/usr/bin/bash

# Assumes we want to debug in Windows Terminal specifically.

# According to MS: 
#   "Execution aliases do not work in WSL distributions. If you want to use
#   wt.exe from a WSL command line, you can spawn it from CMD directly by
#   running `cmd.exe`.
#
#   The /c option tells CMD to terminate after running.
#   The \; backslash + semicolon separates commands.
#   Note to specify a Windows directory as the starting directory for `wsl.exe`
#   that two backslashes `\\` are required."

# `wt.exe --profile Debian` before `split-pane` this will open a new tab.
# cmd.exe /c "wt.exe" split-pane --profile "Debian" --vertical --size 0.4

# Assumes we are using TMUX.
# https://github.com/cyrus-and/gdb-dashboard/wiki/Use-multiple-terminals
gdb-tmux() {
    # Docs: man --pager='less -p split-window \[' tmux
    # Usage:    tmux split-window [-hP] [-l size[%]] [shell-command] [-F fmt]
    #   -h      horizontal split
    #   -v      vertical split
    #   -P      Prints information about window after creation, see `new-window`.
    #   -l      New size of pane in lines (-v) or columns (-h).
    #           If `%` is specified it is a percentage of available space.
    #   -F      Format for printout is #D, a.k.a `#{pane_id}`.
    #           man --pager='less -p ^FORMATS' tmux`
    local id1="$(tmux split-pane -hPF "#D" -l 40% "tail -f /dev/null")"
    local id2="$(tmux split-pane -vPF "#D" -l 60% "tail -f /dev/null")"
    local id3="$(tmux split-pane -vPF "#D" -l 50% "tail -f /dev/null")"

    # Focus back on the pane that called this script as it will act as our
    # main pane, i.e. it will contain source code and the prompt.
    # tmux last-pane
    tmux select-pane -t 0

    # Docs:     man --pager='less -p display-message' tmux
    # Usage:    tmux display-message [options] [-t target-pane] [message]
    #   -p      Print to stdout.
    #   -t      ID of the desired pange to take information from.
    #   $id     The above variable representing the pseudo terminal index.
    local tty1="$(tmux display-message -p -t "$id1" '#{pane_tty}')"
    local tty2="$(tmux display-message -p -t "$id2" '#{pane_tty}')"
    local tty3="$(tmux display-message -p -t "$id3" '#{pane_tty}')"

    # Move the `variables` dashboard to the helper pane, then run GDB.
    # GDB will run as normal.
    gdb --eval-command="dashboard variables -output $tty1" \
        --eval-command="dashboard expressions -output $tty2" \
        --eval-command="dashboard history -output $tty3" \
        "$@"

    # When done running GDB, close the helper pane we created.
    #
    # Docs:     man --pager='less -p kill-pane' tmux
    # Usage:    tmux kill-pane [-a] [-t target-pane]
    #   -t      If -a given, kill all panes except `target_pane`.
    #           Otheriwse kill just `target_pane`.
    tmux kill-pane -t "$id1"
    tmux kill-pane -t "$id2"
    tmux kill-pane -t "$id3"
}

gdb-tmux "$@"

