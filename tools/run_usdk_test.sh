#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
exec python3 ./tools/run_usdk_test.py
