cpp -dM /usr/include/errno.h | grep 'define E' | sort -n -k 3 | egrep \ $1\$
