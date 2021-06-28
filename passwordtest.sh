#!/bin/bash



echo "##### Bad Password Test #####"
echo ""
cat badpassword.conf
echo ""

set -x
./dfc badpassword.conf list
./dfc badpassword.conf "get test.txt"
./dfc badpassword.conf "put badpassword.conf"
set +x

echo ""
echo ""

echo "##### Bad Username Test #####"

echo ""
cat badusername.conf
echo ""

set -x
./dfc badusername.conf list
./dfc badusername.conf "get test.txt"
./dfc badusername.conf "put badpassword.conf"
set +x


