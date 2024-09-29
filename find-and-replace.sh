#!/usr/bin/env bash

FROM=$1
TO=$2

# Dissection:
#   -p <prompt>     Output the string <prompt>.
#   -n 1            Accept 1 character w/o needing to press ENTER.
#   -r              Accept backslashes literally.
# Note:
#   By default, the return value is stored in the automatic variable
#   '$READ'.
# See:
#   https://stackoverflow.com/a/1885534
read -p "We will potentially modify all files in 'src/*'.
Do you wish to continue? " \
    -n 1 \
    -r

# move cursor to newline for slight prettiness
echo

# Dissection:
#   We are using Bash's regular expression matching.
#   !               Negate the result of the following expression.
#   $REPLY          The default output variable for 'read'.
#   =~              Bash regex match operator.
#   <regex>         The pattern to match in.
if [[ ! $REPLY =~ ^[Yy]$ ]]
then
    # Handle exits from shell or function, but don't exit interactive shell.
    [[ "$0" = "$BASH_SOURCE" ]] && exit 1 || return 1
fi

# Dissection:
#   -e              Ex mode, used mainly for non-interactive file editing.
#   -u NONE         Do not load '.vimrc' or 'init.vim' of any kind.
#   -c <command>    Execute <command> after entering.
#   -- <...>        Positional arguments: file names or globs thereof.
vim -e \
    -u NONE \
    -c "bufdo! %s/\v$FROM/$TO/ge" \
    -c ':xa' \
    -- src/*
