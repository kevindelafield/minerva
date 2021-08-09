FROM centos:7

# install packages
RUN yum -y install epel-release
RUN yum -y install centos-release-scl
RUN yum -y groupinstall 'Development Tools'
RUN yum -y install cmake3 which
RUN yum -y install llvm-toolset-7-clang-5.0.1-4.el7.x86_64

ADD tools/files/linaro-aarch64-2017.08-gcc7.1.tar.gz /usr/local/

RUN echo 'export PATH=/usr/local/linaro-aarch64-2017.08-gcc7.1/bin:/opt/rh/llvm-toolset-7/root/usr/bin/:$PATH' >> /root/.bashrc
RUN echo 'export CC="aarch64-linux-gnu-gcc"' >> /root/.bashrc
RUN echo 'export CXX=aarch64-linux-gnu-g++' >> /root/.bashrc
RUN echo 'export LD=aarch64-linux-gnu-ld' >> /root/.bashrc

WORKDIR /ovhttpd
