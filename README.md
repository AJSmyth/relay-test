# Relay Test

ESP32-C3 Super Mini two-relay binary test with an SSD1306 OLED status display
and recovery from missed display updates. Includes a pinned Arduino CLI
setup, build/upload scripts, and an on-device recovery check for Ubuntu/WSL2.

## Current firmware: two-relay binary test

Relay A uses GPIO8; relay B uses GPIO10. OLED remains SDA=4, SCL=5.
GPIO8 also drives the onboard LED on common Super Mini boards.
Every two seconds, the commanded relay states cycle 00 -> 01 -> 10 -> 11,
where A is the high bit and B is the low bit. Startup initializes both off.
The top bordered display cell shows A and the bottom shows B. ON is black
text on white; OFF is white text on black. Display shows commanded states,
not measured contact feedback.

`RELAY_ACTIVE_LOW` in the sketch selects trigger polarity (false: user-confirmed
active-high modules). `STEP_MS` controls the interval.
Relay modules have 5 V coils; their input compatibility must be checked
separately. GPIO8 is a strapping pin: external circuitry holding it low can
prevent download mode. Software cannot guarantee relay state before setup
runs; appropriate hardware biasing is required for startup guarantees.
This test does not provide mechanical interlocking between relay contacts.

### OLED missed-update recovery

The display is redrawn from the current `relayState` every 400 ms, rather
than only at relay transitions. Every I2C command/data packet is checked;
failed or partial frames are retried from the beginning using the latest
state. Horizontal addressing and the column/page window are reset for each
frame. Transfers wait at least 80 ms after switching, and do not start in
the last 200 ms before a relay deadline. Serial diagnostics are nonblocking.
The relay schedule advances against its original deadlines, and redraws
never advance relay state. GPIO polarity remains active-high.

Run `python3 scripts/verify_oled_recovery.py /dev/ttyACM0` in WSL to check
40 seconds of relay transitions and acknowledged display frames. The script
sends `f` twice: this intentionally stops a frame after 256 bytes, then
checks that a subsequent current-state frame succeeds within one second.
The serial log is saved to `build/oled-recovery.log`. This validates software
recovery and bus acknowledgements, not physical pixels or relay contacts.
Persistent wiring/power faults still need hardware correction.

Validation on 2026-11-08: build/upload passed with flash hash verification.
The 40-second hardware test passed 20 sequential transitions, with both
injected partial frames recovering; worst recovery was 0.477 seconds.
No real I2C errors were recorded during that run. Visual confirmation over
longer operation remains pending.

The active-high relay firmware compiled and uploaded with flash hash
verification. Serial capture confirmed all four states in order:
OFF/OFF, OFF/ON, ON/OFF, ON/ON. Physical relay operation and the new OLED
layout still require visual confirmation. The OLED-only test below is historical.

Goal: reproducible WSL2 build/upload workflow and a user-verified OLED alternating
test text and a 16x16 smile bitmap every two seconds.

## Wiring (disconnect USB first)

| OLED label | ESP32-C3 label |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | 4 (GPIO4) |
| SCL or SCK | 5 (GPIO5) |

Use printed labels, not connector position. Assumes a four-pin I2C module.
The marking 0x78 usually denotes the 8-bit write address: use 0x3C in Wire.
The firmware scans the bus and accepts 0x3C or 0x3D, preferring 0x3C.
The user confirmed correct text, bitmap and border rendering with the SSD1306
128x64 driver. This establishes driver compatibility; the exact controller
silicon cannot be identified from an I2C ACK alone.

## USB and WSL

Ubuntu-24.04 is the target WSL2 distribution. Install usbipd-win on Windows.
Start Ubuntu, then run `usbipd list` in PowerShell to identify the board.
In administrator PowerShell, run `usbipd bind --busid <actual-BUSID>`.
In ordinary PowerShell, run `usbipd attach --wsl --busid <actual-BUSID>`.
Do not bind unrelated peripherals. In Ubuntu, run `lsusb` and
`~/.local/bin/arduino-cli board list`; native USB usually uses /dev/ttyACM0.
Add the Linux user to dialout if necessary, then open a new Linux session.
USB disconnection/reset may require attaching again; recheck the BUSID.

Recovery: hold BOOT, press/release RESET, then release BOOT. Alternatively,
hold BOOT while plugging in USB. This enters ROM download mode without erasing
flash and bypasses firmware which disabled USB. After uploading from manual
download mode, press RESET to start the sketch if needed.

## Build and upload

Installed versions: Arduino CLI 1.5.1, ESP32 core 3.3.12, Adafruit SSD1306
2.5.17, Adafruit GFX 1.13.0, Adafruit BusIO 1.17.4. On a fresh Ubuntu setup,
run `bash scripts/setup.sh` to install these versions (requires curl and tar).

Run from this repository in Ubuntu (Windows path is accessible under /mnt/c):

```bash
bash scripts/build.sh
bash scripts/upload.sh /dev/ttyACM0
~/.local/bin/arduino-cli monitor -p /dev/ttyACM0 --config baudrate=115200
```

The build uses ESP32C3 Dev Module with USB CDC on boot enabled. Confirm the
physical chip and flash size before uploading. The sketch boots without a
monitor, prints I2C diagnostics, and retries a missing OLED every five seconds.
Close the serial monitor before uploading. Uploading replaces existing firmware.

## Historical OLED-only hardware verification

On 2026-11-08 the board enumerated as USB 303a:1001, Windows COM3, BUSID
1-2, then attached to Ubuntu as /dev/ttyACM0. esptool identified ESP32-C3
revision v0.4, 40 MHz crystal and 4 MB XMC flash. The demo compiled
(336428 bytes program, 15300 bytes global RAM), uploaded successfully, and
all flashed segments passed hash verification. The initial scan found no OLED;
the user corrected a misconnection and confirmed correct text/bitmap/border
alternation. Subsequent serial capture also showed alternating `Page: text`
and `Page: bitmap` messages. The user answered "yup all good" to the final
on-screen address and standalone power-cycle check. The firmware selects the
responding address automatically and displays it on the text page; no numeric
address was separately transcribed into the chat. Standalone restart passed.

Keep a WSL shell open while attaching: usbipd requires a running WSL2 distro.

## Historical OLED-only acceptance checks

- [x] Identify chip and flash over USB before first upload.
- [x] Confirm detected OLED address is displayed on the text page (user check).
- [x] Confirm readable text and an entire border/bitmap without clipping or offset.
- [x] Confirm pages alternate every two seconds.
- [x] Power-cycle and confirm standalone startup, with serial monitor closed.

If there is no ACK, check power and SDA/SCL wiring. If there is an ACK but the
screen is blank or offset, confirm module geometry/controller and consider
SH1106 support rather than assuming the scan identified an SSD1306.

References: [WSL USB setup](https://learn.microsoft.com/en-us/windows/wsl/connect-usb),
[ESP32 package installation](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html),
[USB recovery](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/usb-serial-jtag-console.html).
