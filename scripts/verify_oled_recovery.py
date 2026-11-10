"""On-device 40-second sequence and interrupted-frame recovery check (Linux)."""
import os
import re
import select
import sys
import termios
import time
import tty
from pathlib import Path

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
original = termios.tcgetattr(fd)
records = []
states = []
syncs = []
recoveries = []
pending_failure = None
injected = 0
errors = []
try:
    tty.setraw(fd)
    termios.tcflush(fd, termios.TCIFLUSH)
    start = time.monotonic()
    buffered = b""
    while time.monotonic() - start < 40:
        elapsed = time.monotonic() - start
        if injected < 2 and elapsed >= (5, 15)[injected]:
            os.write(fd, b"f")
            injected += 1
        if not select.select([fd], [], [], 0.1)[0]:
            continue
        buffered += os.read(fd, 4096)
        while b"\n" in buffered:
            raw, buffered = buffered.split(b"\n", 1)
            line = raw.decode(errors="replace").strip()
            records.append(line)
            print(line, flush=True)
            m = re.match(r"State (\d):.* at (\d+) ms", line)
            if m:
                states.append(tuple(map(int, m.groups())))
            m = re.match(r"OLED synced state=(\d) at (\d+) ms ok=(\d+) failed=(\d+)", line)
            if m:
                sync = tuple(map(int, m.groups()))
                syncs.append(sync)
                if states and sync[0] != states[-1][0]:
                    errors.append("Display acknowledged a stale state")
                if pending_failure is not None:
                    recoveries.append(time.monotonic() - pending_failure)
                    pending_failure = None
            if line.startswith("TEST: interrupted"):
                pending_failure = time.monotonic()
            if "OLED transfer error=" in line:
                errors.append(line)
finally:
    termios.tcsetattr(fd, termios.TCSANOW, original)
    os.close(fd)
    Path("build").mkdir(exist_ok=True)
    Path("build/oled-recovery.log").write_text("\n".join(records) + "\n")

assert len(states) >= 18, f"Too few transitions: {len(states)}"
for (before, t0), (after, t1) in zip(states, states[1:]):
    assert after == (before + 1) % 4, "Skipped relay state"
    assert 1950 <= t1 - t0 <= 2050, f"Unexpected step interval: {t1-t0} ms"
assert len(recoveries) == 2, f"Recovered {len(recoveries)} / 2 interruptions"
assert max(recoveries) < 1.0, f"Slow recovery: {recoveries}"
assert len(syncs) >= 18, "Too few acknowledged display frames"
assert not errors, errors
print(f"PASS: {len(states)} transitions, 2 recoveries; worst recovery {max(recoveries):.3f}s")
