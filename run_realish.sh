#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

echo "[realish] build active boot image"
./build.sh

LOG_DIR=./logs
OVMF_VARS=./bin/OVMF_VARS_4M.realish.fd
SERIAL_LOG=$LOG_DIR/serial_q35_8g.log
QEMU_LOG=$LOG_DIR/qemu_q35_8g.log

QEMU_DISPLAY=${QEMU_DISPLAY:-gtk}
QEMU_CPU=${QEMU_CPU:-max}
QEMU_MEM=${QEMU_MEM:-8G}
QEMU_VGA=${QEMU_VGA:-std}
REALISH_USB_KBD=${REALISH_USB_KBD:-0}

if [ -n "${QEMU_SERIAL:-}" ]; then
  SERIAL_TARGET=$QEMU_SERIAL
elif [ "$QEMU_DISPLAY" = "none" ]; then
  SERIAL_TARGET=stdio
else
  SERIAL_TARGET=file:$SERIAL_LOG
fi

mkdir -p "$LOG_DIR"
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$OVMF_VARS"
rm -f "$SERIAL_LOG" "$QEMU_LOG"

echo "[realish] machine: q35"
echo "[realish] memory: $QEMU_MEM"
echo "[realish] cpu: $QEMU_CPU"
echo "[realish] vga: $QEMU_VGA"
echo "[realish] usb keyboard: $REALISH_USB_KBD"
echo "[realish] display: $QEMU_DISPLAY"
echo "[realish] serial: $SERIAL_TARGET"
if [ "$SERIAL_TARGET" != "stdio" ]; then
  echo "[realish] serial log: $SERIAL_LOG"
fi
echo "[realish] qemu log: $QEMU_LOG"

USB_INPUT_DEVICES=(-device usb-tablet,bus=xhci.0)
if [ "$REALISH_USB_KBD" = "1" ]; then
  USB_INPUT_DEVICES+=(-device usb-kbd,bus=xhci.0)
else
  echo "[realish] using default PS/2 keyboard path for current OS keyboard driver"
fi

qemu-system-x86_64 \
  -machine q35 \
  -m "$QEMU_MEM" \
  -cpu "$QEMU_CPU" \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file="$OVMF_VARS" \
  -drive if=none,id=uefi_esp,format=raw,file=./bin/uefi_esp.img \
  -device virtio-blk-pci,drive=uefi_esp,bootindex=1 \
  -device qemu-xhci,id=xhci \
  "${USB_INPUT_DEVICES[@]}" \
  -netdev user,id=net0 \
  -device e1000e,netdev=net0 \
  -boot menu=off \
  -vga "$QEMU_VGA" \
  -display "$QEMU_DISPLAY" \
  -serial "$SERIAL_TARGET" \
  -monitor none \
  -no-reboot \
  -d guest_errors,cpu_reset,int \
  -D "$QEMU_LOG"
