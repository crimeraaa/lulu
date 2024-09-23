#!/usr/bin/env bash

FROM=$1
TO=$2

# Dissection:
#   -e              Ex mode, used mainly for non-interactive file editing.
#   -u NONE         Do not load '.vimrc' or 'init.vim' of any kind.
#   -c <command>    Execute <command> after entering.
#   -- <...>        Positional arguments: file names or globs thereof.
vim -e -u NONE -c "bufdo! %s/\v$FROM/$TO/ge" -c ':xa' -- src/*
