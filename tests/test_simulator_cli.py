#!/usr/bin/env python3
"""启动失败必须非零；缺配置、错误参数、不可用 CAN、日志失败均不伪报成功。"""
import json
from pathlib import Path
import subprocess
import sys


def main():
    binary = Path(sys.argv[1]).resolve()
    directory = binary.parent / "cli_checks"
    directory.mkdir(exist_ok=True)
    base = {"role": "bms", "version": 2015, "can_interface": "sim_missing0", "file_log": False}
    cases = [({}, "role/version"), ({**base, "role": "invalid"}, "role/version"),
             ({**base, "parameters": {"bcl_current_ma": 400001}}, "parameters"),
             ({**base, "parameters": {"bro_ready": 1}}, "parameters"),
             ({**base, "unknown": 1}, "unknown"),
             ({**base, "candump": "yes"}, "candump"),
             ({**base, "candump_path": 1}, "candump_path"),
             ({**base, "source": 1, "peer": 1}, "cross-field"),
             ({**base, "file_log": True, "log_path": str(directory / "missing/file.log")}, "log initialization failed"),
             (base, "can_bus_open failed")]
    if Path("/dev/full").exists():
        cases.append(({**base, "file_log": True, "log_path": "/dev/full"}, "log initialization failed"))
    auto_path = directory / "auto.json"
    auto_path.write_text(json.dumps({"role": "bms", "version": 2015, "can_interface": "sim_missing0"}), encoding="utf-8")
    existing = set((directory / "logs").glob("*.log"))
    for _ in range(2):
        result = subprocess.run([str(binary), "--config", str(auto_path)], cwd=directory, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=3)
        if result.returncode != 1 or "LOGFILE" not in result.stdout:
            print(f"FAIL: generated log: {result.stdout}")
            return 1
    generated = set((directory / "logs").glob("*.log")) - existing
    import re
    if len(generated) != 2 or any(not re.fullmatch(r"\d{8}_\d{6}_\d{9}_\d+\.log", path.name) or
                                  "can_bus_open failed" not in path.read_text() for path in generated):
        print("FAIL: timestamp names, collision prevention or exit-time log drain")
        return 1
    for index, (config, token) in enumerate(cases):
        path = directory / f"case_{index}.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        result = subprocess.run([str(binary), "--config", str(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=3)
        if result.returncode != 1 or token not in result.stdout:
            print(f"FAIL: {index}, exit={result.returncode}, output={result.stdout}")
            return 1
    for speed in ("0", "-1", "1x", "4294967297", "101"):
        result = subprocess.run([str(binary), "--speed", speed], stdout=subprocess.PIPE, text=True, timeout=3)
        if result.returncode != 1 or "Invalid --speed" not in result.stdout:
            print(f"FAIL: speed {speed}")
            return 1
    for content in (b'{"role":"bms","version":2015}\0trailing', b"x" * 65537):
        path = directory / "bad_source.json"
        path.write_bytes(content)
        result = subprocess.run([str(binary), "--config", str(path)], stdout=subprocess.PIPE, text=True, timeout=3)
        if result.returncode != 1 or "config source" not in result.stdout:
            print("FAIL: malformed source")
            return 1
    print("PASS: startup validation, CAN/log failures, strict speed and bounded configuration source")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
