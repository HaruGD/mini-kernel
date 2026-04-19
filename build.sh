#/bin/bash
export PREFIX="%HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
export PATH="$PATH:/home/user/opt/cross/bin"
make all