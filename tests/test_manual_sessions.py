#!/usr/bin/env python3
"""Persistent manual sessions over real SocketCAN; absent vcan is SKIP, not PASS."""
import json
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def wait_for(predicate, description, processes):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if predicate():
            return
        if any(p.poll() is not None for p in processes):
            raise AssertionError(f"unexpected process exit: {description}")
        time.sleep(0.02)
    raise AssertionError(f"timeout: {description}")


def main():
    probe = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    try:
        probe.bind(("vcan0",))
    except OSError:
        print("SKIP: vcan0 unavailable")
        return 77
    finally:
        probe.close()
    binary = str(Path(sys.argv[1]).resolve())
    directory = Path(tempfile.mkdtemp(prefix="manual_sessions_", dir=Path(binary).parent))
    processes = []

    def launch(role, version, suffix="", **overrides):
        name = f"{role}_{version}{suffix}"
        logfile = directory / f"{name}.log"
        config = {"role": role, "version": version, "start_mode": "manual", "can_interface": "vcan0",
                  "software_test": True, "speed": 10, "file_log": True, "log_path": str(logfile),
                  "stop_after_ms": 1000 if role == "charger" else 0, "max_session_ms": 15000,
                  "recovery_wait_ms": 100, "aux_power_off_delay_ms": 100}
        config.update(overrides)
        path = directory / f"{name}.json"
        path.write_text(json.dumps(config))
        output = (directory / f"{name}.console").open("w")
        process = subprocess.Popen([binary, "--config", str(path)], stdin=subprocess.PIPE,
                                   stdout=output, stderr=subprocess.STDOUT)
        output.close()
        processes.append(process)
        read = lambda: logfile.read_text() if logfile.exists() else ""
        wait_for(lambda: "IDLE" in read(), f"{name} startup", [process])
        return process, read

    def enter(process):
        process.stdin.write(b"\n")
        process.stdin.flush()

    try:
        for version in (2015, 2023):
            bms, bread = launch("bms", version)
            charger, cread = launch("charger", version)
            charger.stdin.write(b"start\n")
            charger.stdin.flush()
            time.sleep(1.6)  # Longer than max_session_ms at speed=10.
            assert not re.search(r"^TX\s", cread(), re.MULTILINE) and not re.search(r"^TX\s", bread(), re.MULTILINE)
            assert charger.poll() is None and bms.poll() is None
            for number in (1, 2):
                enter(charger)
                wait_for(lambda: all(f"id={number} status=PASS" in read() for read in (cread, bread)),
                         f"{version} session {number}", [charger, bms])
                before = len(re.findall(r"^TX\s", cread(), re.MULTILINE))
                time.sleep(0.15)
                assert charger.poll() is None and bms.poll() is None
                assert len(re.findall(r"^TX\s", cread(), re.MULTILINE)) == before, "charger restarted without Enter"
            # Ctrl-C active stops the session and preserves both processes.
            enter(charger)
            wait_for(lambda: "id=3 event=start" in cread(), "third start", [charger, bms])
            charger.send_signal(signal.SIGINT)
            wait_for(lambda: "id=3 status=" in cread(), "Ctrl-C session stop", [charger, bms])
            assert charger.poll() is None
            charger.send_signal(signal.SIGINT)
            bms.send_signal(signal.SIGTERM)
            for process in (charger, bms):
                assert process.wait(timeout=5) == 0
        charger, read = launch("charger", 2015, "_failure", messages={"BHM": {"first_timeout_ms": 100, "timeout_ms": 100}})
        for number in (1, 2):
            enter(charger)
            wait_for(lambda: f"id={number} status=FAIL" in read(), "failure returns idle", [charger])
            assert charger.poll() is None
        bms, bread = launch("bms", 2015, "_recovery")
        enter(charger)
        wait_for(lambda: "id=3 status=PASS" in read() and "id=1 status=PASS" in bread(),
                 "successful session after failures", [charger, bms])
        bms.send_signal(signal.SIGTERM)
        assert bms.wait(timeout=5) == 0
        charger.stdin.close()
        assert charger.wait(timeout=5) == 1, "failed session history must remain visible on exit"
        bms, bread = launch("bms", 2015, "_realtime", software_test=False, speed=1)
        charger, read = launch("charger", 2015, "_realtime", software_test=False, speed=1)
        enter(charger)
        wait_for(lambda: "id=1 status=PASS" in read() and "id=1 status=PASS" in bread(),
                 "real-time manual session", [charger, bms])
        charger.stdin.close()
        bms.send_signal(signal.SIGTERM)
        for process in (charger, bms):
            assert process.wait(timeout=5) == 0
        print(f"PASS: manual repeated sessions, passive BMS, Ctrl-C, failures and EOF; evidence={directory}")
        return 0
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
            process.wait()


if __name__ == "__main__":
    sys.exit(main())
