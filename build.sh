#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

echo "[build] build OS64 FAT32 root image"
make all64

echo "[build] build UEFI ESP image"
make uefi
