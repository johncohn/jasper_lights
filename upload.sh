#!/usr/bin/env bash
# Builds Jasper Lights and uploads it to the M5. Use this rather than the Arduino
# IDE: the flags below give FastLED's LED driver its full buffer (8 RMT blocks, no
# adaptive planner), which keeps WiFi interrupts from glitching the LEDs. The IDE
# can't pass them, and FastLED compiles its driver separately, so a #define in the
# sketch wouldn't reach it.
#
# Usage: ./upload.sh            (finds the M5's USB port)
#        ./upload.sh <port>
#        ./upload.sh --compile  (build only)
set -euo pipefail
cd "$(dirname "$0")/.."

FQBN=m5stack:esp32:m5stack_stickc_plus2
FLAGS="-DFASTLED_RMT_STATIC_ALLOCATION=1 -DFASTLED_RMT_MEM_BLOCKS=8"
ARGS=(--fqbn "$FQBN" --build-property "compiler.cpp.extra_flags=$FLAGS"
      --build-property "compiler.c.extra_flags=$FLAGS")

if [[ "${1:-}" == "--compile" ]]; then
  exec arduino-cli compile "${ARGS[@]}" jasper_lights
fi
PORT="${1:-$( (ls /dev/cu.usbserial-* 2>/dev/null || true) | head -1)}"
[[ -n "$PORT" ]] || { echo "M5 not found: plug it in, or give the port"; exit 1; }
echo "Uploading to $PORT"
arduino-cli compile "${ARGS[@]}" -u -p "$PORT" jasper_lights
