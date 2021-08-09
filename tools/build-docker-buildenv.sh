#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $DIR/../
if [ -z "$1" ]
then
	sudo docker build -t ovhttpd-build .
elif [ "$1" == "rebuild" ]
then
	sudo docker build -t ovhttpd-build --no-cache .
fi
