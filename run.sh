#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

echo "[uefi] build active boot image"
./build.sh

OVMF_VARS=./bin/OVMF_VARS_4M.run.fd
LOG_DIR=./logs
SERIAL_LOG=$LOG_DIR/serial_uefi_run.log
QEMU_LOG=$LOG_DIR/qemu_uefi_run.log
QEMU_DISPLAY=${QEMU_DISPLAY:-gtk}
SERIAL_TARGET=${QEMU_SERIAL:-file:$SERIAL_LOG}
if [ "$QEMU_DISPLAY" = "none" ] && [ -z "${QEMU_SERIAL:-}" ]; then
  SERIAL_TARGET=stdio
fi

mkdir -p "$LOG_DIR"
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$OVMF_VARS"
rm -f "$SERIAL_LOG" "$QEMU_LOG"

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
  -serial "$SERIAL_TARGET" \
  -monitor none \
  -no-reboot \
  -d guest_errors \
  -D "$QEMU_LOG"
