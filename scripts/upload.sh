#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CLI="${ARDUINO_CLI:-$HOME/.local/bin/arduino-cli}"
: "${1:?Usage: bash scripts/upload.sh /dev/ttyACM0}"
"$CLI" upload --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc --port "$1" --input-dir build firmware/oled_demo
