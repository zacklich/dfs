#!/bin/bash


# random file sizes

SIZES="5344 7904 1280 1248 13152 8256 8096 13856 6880 11104 10656 1920 7264 3808 15552 3040 11104 13408 13280"
SIZES="$SIZES 23296 16384 22912 41344 21248 44672 47232 58240 49664 57344 30848 45184 35840 47488 31744 55808 25216"

# https://www.cyberciti.biz/faq/howto-check-if-a-directory-exists-in-a-bash-shellscript/
# https://unix.stackexchange.com/questions/33629/how-can-i-populate-a-file-with-random-data

# Create some test files

rm -rf tmp
mkdir -p tmp
for i in $SIZES; do
    head -c $i /dev/urandom > tmp/testfile_$i
done

ls tmp
