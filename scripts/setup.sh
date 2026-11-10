#!/usr/bin/env bash
set -euo pipefail
mkdir -p "$HOME/.local/bin"
installer=$(mktemp)
trap 'rm -f "$installer"' EXIT
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh -o "$installer"
BINDIR="$HOME/.local/bin" sh "$installer" 1.5.1
CLI="$HOME/.local/bin/arduino-cli"
"$CLI" config init --overwrite=false
"$CLI" config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
"$CLI" core update-index
"$CLI" core install esp32:esp32@3.3.12
"$CLI" lib install 'Adafruit BusIO@1.17.4' 'Adafruit GFX Library@1.13.0' 'Adafruit SSD1306@2.5.17'
