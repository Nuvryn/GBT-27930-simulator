#!/usr/bin/env python3
"""Real-time two-process SocketCAN with production UI/controller and a test-only memory display."""
import json
import errno
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import traceback


def live_text(path):
    try:
        return path.read_text()
    except FileNotFoundError:
        return ""
    except OSError as error:
        # WSL /mnt/d can transiently report ENODATA while another process grows a file.
        # Retry only this filesystem read; protocol deadlines and evidence remain unchanged.
        if error.errno == errno.ENODATA:
            return ""
        raise


def main():
    binary = Path(sys.argv[1]).resolve()
    probe = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    try:
        probe.bind(("vcan0",))
    except OSError:
        print("SKIP: vcan0 unavailable")
        return 77
    finally:
        probe.close()
    directory = Path(tempfile.mkdtemp(prefix="ui_vcan_", dir=binary.parent))
    report = []
    for version in (2015, 2023):
        processes = {}
        logs = {}
        item = {"version": version, "status": "FAIL", "evidence": str(directory)}
        try:
            for role in ("bms", "charger"):
                name = f"{version}_{role}"
                logs[role] = directory / f"{name}.log"
                config = {"role": role, "version": version, "start_mode": "manual", "can_interface": "vcan0",
                          "file_log": True, "log_path": str(logs[role]), "stop_after_ms": 1000 if role == "charger" else 0,
                          "max_session_ms": 15000, "recovery_wait_ms": 100,
                          "ui": {"enabled": True, "input_device": "/test/memory"}}
                path = directory / f"{name}.json"
                path.write_text(json.dumps(config))
                with (directory / f"{name}.console").open("w") as output:
                    processes[role] = subprocess.Popen([str(binary), "--config", str(path)], stdin=subprocess.DEVNULL,
                                                       stdout=output, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if any(process.poll() is not None for process in processes.values()):
                    raise AssertionError("UI process exited before two sessions completed")
                if all("id=2 status=PASS" in live_text(path) for path in logs.values()):
                    break
                time.sleep(0.02)
            else:
                raise AssertionError("two UI-triggered sessions did not complete")
            for process in processes.values():
                process.send_signal(signal.SIGTERM)
            for process in processes.values():
                assert process.wait(timeout=5) == 0
            timing = {}
            for role, path in logs.items():
                text = path.read_text()
                assert len(re.findall(r"^SESSION_RESULT\s+.*status=PASS", text, re.M)) == 2
                assert "status=FAIL" not in text and "lost data" not in text
                assert len(re.findall(r"SESSION_CONFIG_END id=", text)) == 2, "missing per-session configuration evidence"
                last_dt = last_periodic = None
                periodic_gaps, dt_gaps = [], []
                for line in text.splitlines():
                    if re.match(r"SESSION\s", line):
                        last_dt = last_periodic = None
                    match = re.match(r"TX\s+\[(\d+) ms\] id=([0-9A-F]+) len=\d+ data=(.*)", line)
                    if not match:
                        continue
                    stamp, identifier, payload = match.groups()
                    stamp, identifier = int(stamp), int(identifier, 16)
                    pgn = (identifier >> 16) & 255
                    if pgn == 0xEB:
                        if int(payload.split()[0], 16) > 1 and last_dt is not None:
                            dt_gaps.append(stamp-last_dt)
                        last_dt = stamp
                    if pgn == (0x10 if role == "bms" else 0x12):
                        if last_periodic is not None:
                            periodic_gaps.append(stamp-last_periodic)
                        last_periodic = stamp
                assert periodic_gaps and min(periodic_gaps) >= 50 and max(periodic_gaps) <= 100, (role, periodic_gaps)
                if role == "bms":
                    assert dt_gaps and min(dt_gaps) >= 10, dt_gaps
                timing[role] = {"periodic_gaps_ms": periodic_gaps, "dt_gaps_ms": dt_gaps}
            item.update(status="PASS", timing=timing)
        except Exception as error:
            item["error"] = str(error)
            item["traceback"] = traceback.format_exc()
        finally:
            for process in processes.values():
                if process.poll() is None:
                    process.kill()
                process.wait()
            report.append(item)
    (directory / "report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    return 0 if all(item["status"] == "PASS" for item in report) else 1


if __name__ == "__main__":
    sys.exit(main())
