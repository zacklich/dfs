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

echo "### type 'make clear' in the server window, then"
echo "### type 'make start123' in the server window, then"
echo "### press ENTER"
read xxx

( cd tmp ; 

    for f in testfile_5*; do
        echo ""
        echo "../dfc $CONFIGFILE \"put $f inctest_$f.dfs\""
        ../dfc $CONFIGFILE "put $f inctest_$f.dfs"
    done

    rm -f *_check_*

    for f in testfile_5*; do
        echo ""
        echo "../dfc $CONFIGFILE \"get inctest_$f.dfs ${CONFIGNAME}_check_$f\""
        ../dfc $CONFIGFILE "get inctest_$f.dfs ${CONFIGNAME}_check_$f"
    done
)


echo "### type 'make start234' in the server window and press ENTER ###"
read xxx

echo "### listing remote files ###"

./dfc $CONFIGFILE "list"

echo "### try to read incomplete files ###"

(cd tmp ; 

    rm -f *_check_*

    for f in testfile_5*; do
        echo ""
        echo "../dfc $CONFIGFILE \"get inctest_$f.dfs ${CONFIGNAME}_check_$f\""
        ../dfc $CONFIGFILE "get inctest_$f.dfs ${CONFIGNAME}_check_$f"
    done
)
