#!/usr/bin/env bash

FROM=$1
TO=$2
vim -e -u NONE -c "bufdo! %s/\v$FROM/$TO/ge" -c ':xa' -- src/*
