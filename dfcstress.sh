#!/bin/bash

CONFIGFILE=$1

if [ "$CONFIGFILE" == "" ]; then
    echo "You need to specify the config file name for dfc"
    exit 1
fi

# https://stackoverflow.com/questions/965053/extract-filename-and-extension-in-bash
CONFIGNAME="${CONFIGFILE%.*}"

CONFIGFILE="`pwd`/$CONFIGFILE"


if [ ! -d tmp ] ; then
    echo "Need to create test files first, run ./maketestfiles.sh"
    exit 1
fi

cd tmp


for iter in {1..10}; do
    for f in testfile_*; do
        echo ""
        echo "../dfc $CONFIGFILE \"put $f $f.dfs\""
        ../dfc $CONFIGFILE "put $f $f.dfs"
    done

    rm -f check_*

    for f in testfile_*; do
        echo ""
        echo "../dfc $CONFIGFILE \"get $f.dfs ${CONFIGNAME}_check_$f\""
        ../dfc $CONFIGFILE "get $f.dfs ${CONFIGNAME}_check_$f"
    done

    for f in testfile_*; do
        cmp $f ${CONFIGNAME}_check_$f
        if [ $? != 0 ]; then
            echo "####### FILE COMPARE ERROR $f vs ${CONFIGNAME}_check_$f ######"
        fi
    done
done

