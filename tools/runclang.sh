#!/bin/bash

cmd="clang-9"
which $cmd > /dev/null 2>&1
if [ "$?" -ne "0" ]; then
    cmd="clang-8"
    which $cmd > /dev/null 2>&1
    if [ "$?" -ne "0" ]; then
        cmd="clang-7"
        which $cmd > /dev/null 2>&1
        if [ "$?" -ne "0" ]; then
            cmd="clang"
            which $cmd > /dev/null 2>&1
            if [ "$?" -ne "0" ]; then
      	        echo clang n  ot installed;
	            exit 0;
            fi
        fi
    fi
fi

echo "RUNNING CLANG: $cmd $@"

outfile=/tmp/clangoutput.$$

function finish
{
    rm -f $outfile
}

trap finish EXIT

$cmd --analyze $@ > $outfile 2>&1
cat $outfile

lines=`grep warning: $outfile | wc -l`
if [ "$lines" -gt "0" ]; then
	exit 1;
fi

lines=`grep error: $outfile | wc -l`
if [ "$lines" -gt "0" ]; then
	exit 1;
fi

exit 0;
