#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ESP=/tmp/os64_usdk_test_esp.img
VARS=/tmp/os64_usdk_test_vars.fd
MONITOR=/tmp/os64_usdk_test_monitor.sock
SERIAL=./logs/serial_usdk_test.log
QEMU_LOG=./logs/qemu_usdk_test.log
MONITOR_OUTPUT=/tmp/os64_usdk_test_monitor.txt

cleanup() {
    if [ -n "${QEMU_PID:-}" ]; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi
    rm -f "$ESP" "$VARS" "$MONITOR" "$MONITOR_OUTPUT"
}
trap cleanup EXIT

mkdir -p ./logs
rm -f "$ESP" "$VARS" "$MONITOR" "$SERIAL" "$QEMU_LOG" "$MONITOR_OUTPUT"
cp ./bin/uefi_esp.img "$ESP"
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$VARS"

qemu-system-x86_64 \
    -machine q35 \
    -m 512M \
    -cpu max \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file="$VARS" \
    -drive if=none,id=esp,format=raw,file="$ESP" \
    -device virtio-blk-pci,drive=esp,bootindex=1 \
    -boot menu=off \
    -vga std \
    -display none \
    -serial file:"$SERIAL" \
    -monitor unix:"$MONITOR",server,nowait \
    -no-reboot \
    -d guest_errors,cpu_reset,int \
    -D "$QEMU_LOG" &
QEMU_PID=$!

for _ in $(seq 1 50); do
    if [ -S "$MONITOR" ]; then
        break
    fi
    sleep 0.1
done

sleep 8
{
    echo "sendkey r"
    echo "sendkey u"
    echo "sendkey n"
    echo "sendkey spc"
    echo "sendkey u"
    echo "sendkey s"
    echo "sendkey d"
    echo "sendkey k"
    echo "sendkey shift-minus"
    echo "sendkey t"
    echo "sendkey e"
    echo "sendkey s"
    echo "sendkey t"
    echo "sendkey dot"
    echo "sendkey e"
    echo "sendkey l"
    echo "sendkey f"
    echo "sendkey ret"
} | timeout 2s nc -U "$MONITOR" >"$MONITOR_OUTPUT" || true

sleep 8

if ! grep -q "=== result: passed=" "$SERIAL"; then
    echo "SDK test did not complete. See $SERIAL" >&2
    exit 1
fi
if ! grep -q "failed=0 ===" "$SERIAL"; then
    echo "SDK test reported a failure. See $SERIAL" >&2
    exit 1
fi

grep -E "\[PASS\]|\[FAIL\]|=== result:" "$SERIAL"
