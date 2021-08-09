#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $DIR/../
mkdir -p bin
sudo docker run -v $PWD/src:/ovhttpd/src:Z -v $PWD/thirdparty:/ovhttpd/thirdparty:Z -v $PWD/tools:/ovhttpd/tools:Z -v $PWD/bin:/ovhttpd/bin:Z -it ovhttpd-build /bin/bash
