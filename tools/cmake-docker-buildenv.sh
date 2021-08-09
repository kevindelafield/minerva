#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $DIR/../
mkdir -p bin
sudo docker run -v $PWD/src:/ovhttpd/src:Z -v $PWD/thirdparty:/ovhttpd/thirdparty:Z -v $PWD/tools:/ovhttpd/tools:Z -v $PWD/bin:/ovhttpd/bin:Z -it ovhttpd-build /bin/bash -c "source /root/.bashrc && mkdir /ovhttpd/build && cd /ovhttpd/build && cmake3 -D AARCH_TOOLCHAIN_DIR=/usr/local/linaro-aarch64-2017.08-gcc7.1 ../src && make -j$(nproc) && cp /ovhttpd/build/www/www /ovhttpd/bin && cp /ovhttpd/build/wifi/wifi /ovhttpd/bin && cp /ovhttpd/build/powerctrl/powerctrl /ovhttpd/bin && rm -rf /ovhttpd/build"
