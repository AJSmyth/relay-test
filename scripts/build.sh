#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CLI="${ARDUINO_CLI:-$HOME/.local/bin/arduino-cli}"
"$CLI" compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc --output-dir build firmware/oled_demo
