#!/bin/bash
qemu-system-i386 \
  -drive format=raw,file=./bin/os.bin,if=ide,index=0 \
  -drive format=raw,file=./bin/disk.img,if=ide,index=1 \
  -d int,cpu_reset -D qemu.log
