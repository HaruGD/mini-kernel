#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "[uefi] build os64 image"
make all64
echo "[uefi] build esp image"
make uefi

OVMF_VARS=./bin/OVMF_VARS_4M.run.fd
SERIAL_LOG=./serial_uefi_run.log
QEMU_LOG=./qemu_uefi_run.log
QEMU_DISPLAY=${QEMU_DISPLAY:-gtk}
SERIAL_TARGET=${QEMU_SERIAL:-file:$SERIAL_LOG}
if [ "$QEMU_DISPLAY" = "none" ] && [ -z "${QEMU_SERIAL:-}" ]; then
  SERIAL_TARGET=stdio
fi
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$OVMF_VARS"
rm -f "$SERIAL_LOG" "$QEMU_LOG"

QEMU_ARGS=(
  -drive format=raw,file=./bin/os64.bin,if=ide,index=0
)

echo "[uefi] starting qemu"
echo "[uefi] display: $QEMU_DISPLAY"
echo "[uefi] serial: $SERIAL_TARGET"
if [ "$SERIAL_TARGET" != "stdio" ]; then
  echo "[uefi] serial log: $SERIAL_LOG"
fi
echo "[uefi] qemu log: $QEMU_LOG"

qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file="$OVMF_VARS" \
  -drive if=none,id=uefi_esp,format=raw,file=./bin/uefi_esp.img \
  -device virtio-blk-pci,drive=uefi_esp,bootindex=1 \
  -boot menu=off \
  -vga std \
  -display "$QEMU_DISPLAY" \
  "${QEMU_ARGS[@]}" \
  -serial "$SERIAL_TARGET" \
  -monitor none \
  -no-reboot \
  -d guest_errors \
  -D "$QEMU_LOG"
