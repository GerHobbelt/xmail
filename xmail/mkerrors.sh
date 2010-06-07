#!/bin/sh

CURR=0

for e in `grep '__ERR_' Errors.h | grep -v '\-' | egrep -o '__[^, ]+' `; do
    echo "$CURR --> $e"
    CURR=`expr $CURR + 1`
done | sed 's/__//g'

