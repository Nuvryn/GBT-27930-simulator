#!/usr/bin/env python3
"""启动失败必须非零；缺配置、错误参数、不可用 CAN、日志失败均不伪报成功。"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import re


def snapshot_from_log(text):
    parts = re.findall(r"^DETAIL\s+\[\d+ ms\] CONFIG part=(\d+) data=(.*)$", text, re.MULTILINE)
    if not parts or [int(part) for part, _ in parts] != list(range(1, len(parts) + 1)) or "CONFIG_END" not in text:
        raise ValueError("missing, incomplete or out-of-order startup configuration")
    return json.loads("".join(data for _, data in parts))


def main():
    binary = Path(sys.argv[1]).resolve()
    directory = Path(tempfile.mkdtemp(prefix="cli_checks_", dir=binary.parent))
    version = subprocess.run([str(binary), "--config", "missing.json", "--version"], cwd=directory,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=3)
    if (version.returncode != 0 or not re.search(r"GB/T 27930 Simulator v\d+\.\d+\.\d+", version.stdout)
            or not re.search(r"Build: \d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", version.stdout)
            or "Target: linux/" not in version.stdout or "Debug" in version.stdout or "Release" in version.stdout
            or list(directory.iterdir())):
        print(f"FAIL: side-effect-free --version: {version.stdout}")
        return 1
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
    existing = set((directory / "logs").glob("*.log"))
    for role in ("bms", "charger"):
        auto_path.write_text(json.dumps({"role": role, "version": 2015, "can_interface": "sim_missing0"}), encoding="utf-8")
        result = subprocess.run([str(binary), "--config", str(auto_path)], cwd=directory, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=3)
        if result.returncode != 1 or "LOGFILE" not in result.stdout:
            print(f"FAIL: generated log: {result.stdout}")
            return 1
        if "DETAIL" in result.stdout or "CONFIG_BEGIN" in result.stdout or "Startup complete" in result.stdout:
            print(f"FAIL: startup console detail leakage or false readiness: {result.stdout}")
            return 1
    generated = set((directory / "logs").glob("*.log")) - existing
    if any((directory / "logs" / f".{path.stem}.session").read_text() != "GBT27930_LOG_SESSION_V1\n1\n" for path in generated):
        print("FAIL: automatic logs lack session ownership records")
        return 1
    if len(generated) != 2 or {path.stem.split("_")[-1] for path in generated} != {"bms", "charger"} or any(not re.fullmatch(r"\d{8}_\d{6}_(bms|charger)\.log", path.name) or
                                  "can_bus_open failed" not in path.read_text() for path in generated):
        print("FAIL: timestamp names, collision prevention or exit-time log drain")
        return 1
    for path in generated:
        text = path.read_text()
        effective = snapshot_from_log(text)
        role = path.stem.split("_")[-1]
        if (effective["role"] != role or effective["dt_interval_ms"] != 10 or effective["file_log"] is not True
                or len(effective["messages"]) != 22 or "software_version=0.1.0" not in text or "compiler=" not in text):
            print("FAIL: default configuration/build evidence")
            return 1
    large = {"role": "bms", "version": 2023, "software_test": True, "speed": 2,
             "can_interface": "sim_missing0", "log_path": "large_startup.log", "candump_path": "\x01" * 240,
             "parameters": {"bcl_current_ma": 50000}, "cell_voltage_mv": [3500] * 256,
             "cell_group": [3] * 256, "temperature_degc": [25] * 128,
             "manufacturer": '"ABC', "software_bytes": list(range(8)), "reserved_bytes": list(range(16)),
             "messages": {"BCS": {"period_ms": 999, "first_timeout_ms": 1234, "timeout_ms": 5678}},
             "faults": [dict(stage="*", message="TP_DT", action="corrupt", byte=0, value=0, count=1)] * 32,
             "events": [dict(stage="charging", action="set_parameter", parameter="bcl_current_ma", value=50000, after_ms=100)] * 32}
    large_path = directory / "large.json"
    large_path.write_text(json.dumps(large), encoding="utf-8")
    result = subprocess.run([str(binary), "--config", str(large_path), "--speed", "3"], cwd=directory,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=5)
    text = (directory / "large_startup.log").read_text()
    effective = snapshot_from_log(text)
    if (result.returncode != 1 or "can_bus_open failed" not in result.stdout or "DETAIL" in result.stdout
            or effective["speed"] != 3 or effective["parameters"]["bcl_current_ma"] != 50000
            or effective["messages"]["BCS"] != {"enabled": True, "period_ms": 999, "first_timeout_ms": 1234, "timeout_ms": 5678}
            or any(effective[key] != large[key] for key in ("cell_voltage_mv", "cell_group", "temperature_degc", "manufacturer", "software_bytes", "reserved_bytes", "candump_path"))
            or len(effective["faults"]) != 32 or effective["events"] != large["events"] or "speed_override=3" not in text):
        print(f"FAIL: complete effective startup snapshot, override or bounded async output: {result.stdout}")
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
