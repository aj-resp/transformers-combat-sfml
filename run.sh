#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
echo "[run.sh] Building..."
make
echo "[run.sh] Copying binaries to /tmp..."
cp -f arbiter/arbiter_bin /tmp/arbiter 2>/dev/null || cp -f ./arbiter_bin /tmp/arbiter 2>/dev/null || true
cp -f hip/hip_bin /tmp/hip 2>/dev/null || cp -f ./hip_bin /tmp/hip 2>/dev/null || true
cp -f ./hip_gui /tmp/hip_gui
cp -f asp/asp_bin /tmp/asp 2>/dev/null || cp -f ./asp_bin /tmp/asp 2>/dev/null || true
cp -r assets /tmp/assets
chmod +x /tmp/arbiter /tmp/hip /tmp/hip_gui /tmp/asp
echo "[run.sh] Launching Chrono Rift..."
cd /tmp && ./arbiter 2>/dev/null
