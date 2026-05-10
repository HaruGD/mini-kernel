#!/bin/bash
qemu-system-x86_64 \
  -drive format=raw,file=./bin/os64.bin,if=ide,index=0 \
  -serial stdio \
  -monitor none
