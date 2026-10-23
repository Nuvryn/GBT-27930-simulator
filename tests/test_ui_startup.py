#!/usr/bin/env python3
"""UI request must never silently fall back; failed device startup must cleanly exit."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
compiled = sys.argv[2] == "ON"
if compiled:
    probe = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    try:
        probe.bind(("vcan0",))
    except OSError:
        print("SKIP: vcan0 unavailable for UI resource initialization check")
        sys.exit(77)
    finally:
        probe.close()
with tempfile.TemporaryDirectory(prefix="ui_startup_", dir=binary.parent) as directory:
    path = Path(directory) / "config.json"
    path.write_text(json.dumps({"role": "charger", "version": 2015, "start_mode": "manual",
                               "can_interface": "vcan0", "file_log": False,
                               "ui": {"enabled": True, "framebuffer": "/dev/null", "input_device": "/dev/null"}}))
    result = subprocess.run([str(binary), "--config", str(path)], stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=5)
    expected = "UI initialization failed" if compiled else "UI requested but SIM_UI is disabled"
    if result.returncode != 1 or expected not in result.stdout or "Startup complete" in result.stdout:
        print(result.stdout)
        raise AssertionError(f"UI startup did not fail cleanly: {result.returncode}")
print("PASS: explicit UI failure and cleanup; no headless fallback")
