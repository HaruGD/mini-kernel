#!/bin/bash
set -e

QEMU_ARGS=(
  -drive format=raw,file=./bin/os64.bin,if=ide,index=0
)

if [ -f ./bin/fat32.img ]; then
  QEMU_ARGS+=(-drive format=raw,file=./bin/fat32.img,if=ide,index=1)
fi

qemu-system-x86_64 \
  "${QEMU_ARGS[@]}" \
  -serial stdio \
  -monitor none
