#!/usr/bin/env python3
"""双进程 vcan 回归；按独立预期检查结果，生成 JSON 报告并清理进程。"""
import argparse
import copy
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent


def scenarios():
    cases = []
    for version in (2015, 2023):
        for initiator in ("charger", "bms"):
            cases.append(dict(name=f"normal_{version}_{initiator}_stop", version=version,
                              charger={"stop_after_ms": 1200 if initiator == "charger" else 0},
                              bms={"stop_after_ms": 1200 if initiator == "bms" else 0},
                              expect=0, evidence=["to=charging", "to=statistics", "to=done", "status=PASS"]))
    cases += [
        dict(name="optional_long_payloads", version=2015, expect=0,
             bms={"messages": {name: {"enabled": True, "period_ms": 500} for name in ("BMV", "BMT", "BSP")}},
             evidence=["status=PASS"], bms_evidence=["name=BMV", "name=BMT", "name=BSP"]),
        dict(name="bcl_missing", version=2015, expect=1,
             charger={"messages": {"BCL": {"timeout_ms": 300}}},
             bms={"faults": [dict(stage="charging", message="BCL", action="drop", after_ms=200, count=0)]},
             charger_evidence=["reason=message_timeout message=BCL"], bms_evidence=["FAULT", "hit=2"]),
        dict(name="bcl_invalid_mode", version=2015, expect=1,
             charger={"messages": {"BCL": {"timeout_ms": 300}}},
             bms={"faults": [dict(stage="charging", message="BCL", action="corrupt", byte=4, value=255, after_ms=200, count=0)]},
             charger_evidence=["message=BCL reason=decode", "reason=message_timeout message=BCL"]),
        dict(name="tp_sequence_zero", version=2015, expect=1,
             bms={"faults": [dict(stage="identify", message="TP_DT", action="corrupt", byte=0, value=0)]},
             evidence=["status=FAIL"], bms_evidence=["FAULT"]),
        dict(name="not_ready", version=2015, expect=1,
             charger={"ready_timeout_ms": 600}, bms={"ready": False, "ready_timeout_ms": 600},
             any_evidence=["reason=ready_timeout"], evidence=["status=FAIL"]),
        dict(name="selftest_failure", version=2015, expect=1,
             charger={"self_test_pass": False}, charger_evidence=["reason=self_test_failed"]),
        dict(name="precharge_failure", version=2015, expect=1,
             charger={"precharge_pass": False}, charger_evidence=["reason=precharge_failed"]),
        dict(name="bcl_duplicate", version=2015, expect=0,
             bms={"faults": [dict(stage="charging", message="BCL", action="duplicate", count=3)]}, bms_evidence=["hit=3"]),
        dict(name="bcl_delay", version=2015, expect=0,
             bms={"faults": [dict(stage="charging", message="BCL", action="delay", delay_ms=50, after_ms=200, count=2)]}, bms_evidence=["hit=2"]),
        dict(name="bsm_absent_2023", version=2023, expect=0,
             charger={"messages": {"BSM": {"timeout_ms": 300}}}, bms={"messages": {"BSM": {"enabled": False}}},
             forbid=["reason=message_timeout message=BSM"]),
        dict(name="bsm_absent_2015", version=2015, expect=1,
             charger={"messages": {"BSM": {"timeout_ms": 300, "first_timeout_ms": 300}}}, bms={"messages": {"BSM": {"enabled": False}}},
             charger_evidence=["reason=message_timeout message=BSM"]),
        dict(name="bsm_lost_2023", version=2023, expect=1,
             charger={"messages": {"BSM": {"timeout_ms": 300}}},
             bms={"faults": [dict(stage="charging", message="BSM", action="drop", after_ms=300, count=0)]}, charger_evidence=["LOCKOUT"]),
        dict(name="pause_resume_2023", version=2023, expect=0,
             bms={"events": [dict(stage="charging", action="pause", after_ms=300), dict(stage="charging", action="resume", after_ms=700)]},
             charger_evidence=["模拟充电暂停", "模拟充电恢复"]),
        dict(name="foreign_bcl_id", version=2015, expect=1,
             charger={"messages": {"BCL": {"timeout_ms": 300}}},
             bms={"faults": [dict(stage="charging", message="BCL", action="id", value=0x181056F3, after_ms=200, count=0)]},
             charger_evidence=["reason=message_timeout message=BCL"]),
        dict(name="short_bcl", version=2015, expect=1,
             charger={"messages": {"BCL": {"timeout_ms": 300}}},
             bms={"faults": [dict(stage="charging", message="BCL", action="length", value=4, after_ms=200, count=0)]},
             charger_evidence=["message=BCL reason=decode"]),
        dict(name="parameter_event", version=2015, expect=0,
             bms={"events": [dict(stage="charging", action="set_parameter", parameter="bcl_current_ma", value=50000, after_ms=300)]},
             bms_evidence=["action=set_parameter", "68 10 AC 0D 02"]),
        dict(name="version_length_mismatch", version=2015, expect=1,
             bms={"version": 2023, "messages": {"CCS": {"timeout_ms": 300, "first_timeout_ms": 300}}},
             bms_evidence=["message=CCS reason=decode", "reason=message_timeout message=CCS"]),
        dict(name="soc_target_stop", version=2015, expect=0,
             charger={"stop_after_ms": 0}, bms={"events": [dict(stage="charging", action="set_parameter", parameter="bcs_soc_pct", value=100, after_ms=400)]},
             bms_evidence=["reason=soc_target"]),
        dict(name="rx_drop_rule", version=2015, expect=1,
             charger={"messages": {"BCL": {"timeout_ms": 300}}, "faults": [dict(stage="charging", message="BCL", direction="rx", action="drop", count=0, after_ms=200)]},
             charger_evidence=["direction=rx", "reason=message_timeout message=BCL"]),
        dict(name="bounded_drop_window", version=2015, expect=0,
             bms={"faults": [dict(stage="charging", message="BCL", action="drop", count=0, after_ms=200, duration_ms=200)]}, bms_evidence=["FAULT"]),
        dict(name="no_file_logging", version=2015, expect=0,
             charger={"file_log": False}, bms={"file_log": False}, evidence=["status=PASS"]),
        dict(name="retry_recovers", version=2015, expect=1,
             charger={"retries": 2, "messages": {"BCL": {"timeout_ms": 300}}},
             bms={"retries": 2, "faults": [dict(stage="charging", message="BCL", action="drop", after_ms=200, count=8)]},
             evidence=["to=statistics", "reason=statistics_complete", "RETRY"]),
        dict(name="retry_exhausted", version=2015, expect=1,
             charger={"retries": 1, "messages": {"BCL": {"timeout_ms": 300}}},
             bms={"retries": 1, "faults": [dict(stage="charging", message="BCL", action="drop", after_ms=200, count=0)]},
             evidence=["RETRY", "reason=retries_exhausted"]),
        dict(name="unplug_replug", version=2023, expect=0,
             charger={"events": [dict(stage="charging", action="unplug", after_ms=200), dict(stage="*", action="replug", after_ms=2000)]},
             bms={"events": [dict(stage="charging", action="unplug", after_ms=200), dict(stage="*", action="replug", after_ms=2000)]},
             evidence=["action=unplug", "action=replug"]),
    ]
    return cases


def merge(base, override):
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            merge(base[key], value)
        else:
            base[key] = copy.deepcopy(value)
    return base


def run_case(case, binary, directory, speed):
    directory.mkdir(parents=True, exist_ok=True)
    processes, handles, texts, file_logs = {}, [], {}, {}
    result = {"name": case["name"], "status": "FAIL", "errors": []}
    try:
        for role in ("charger", "bms"):
            config = dict(role=role, version=case["version"], can_interface="vcan0", software_test=True,
                          speed=speed, stop_after_ms=2000 if role == "charger" else 0,
                          max_session_ms=12000, retries=0, recovery_wait_ms=500,
                          file_log=True, log_path=str(directory / f"{role}.events.log"))
            merge(config, case.get(role, {}))
            file_logs[role] = config["file_log"]
            (directory / f"{role}.events.log").unlink(missing_ok=True)
            path = directory / f"{role}.json"
            path.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding="utf-8")
            handle = (directory / f"{role}.stdout.log").open("w", encoding="utf-8")
            handles.append(handle)
            processes[role] = subprocess.Popen([str(binary), "--config", str(path)], stdout=handle, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 12 / speed + 8
        for process in processes.values():
            process.wait(timeout=max(0.1, deadline - time.monotonic()))
        for role, process in processes.items():
            text_path = directory / (f"{role}.events.log" if file_logs[role] else f"{role}.stdout.log")
            text = text_path.read_text(encoding="utf-8")
            if not file_logs[role] and (directory / f"{role}.events.log").exists():
                result["errors"].append(f"{role}: disabled file logging created a file")
            texts[role] = text
            if process.returncode != case["expect"]:
                result["errors"].append(f"{role}: exit {process.returncode}, expected {case['expect']}")
            for token in case.get("evidence", []) + case.get(f"{role}_evidence", []):
                if token not in text:
                    result["errors"].append(f"{role}: missing {token}")
            for token in case.get("forbid", []):
                if token in text:
                    result["errors"].append(f"{role}: forbidden {token}")
            if "can_bus_open failed" in text:
                result["errors"].append(f"{role}: CAN initialization failed")
            if speed == 1 and case["name"].startswith("normal_"):
                dt_gaps, periodic_gaps = [], []
                last_dt, last_periodic = None, None
                for stamp, identifier, payload in re.findall(r"TX\s+\[(\d+) ms\] id=([0-9A-F]+) len=\d+ data=([^\n]+)", text):
                    stamp, identifier = int(stamp), int(identifier, 16)
                    pgn = (identifier >> 16) & 255
                    if pgn == 0xEB:
                        sequence = int(payload.split()[0], 16)
                        if sequence > 1 and last_dt is not None:
                            dt_gaps.append(stamp - last_dt)
                        last_dt = stamp
                    if pgn == (0x10 if role == "bms" else 0x12):
                        if last_periodic is not None:
                            periodic_gaps.append(stamp - last_periodic)
                        last_periodic = stamp
                if not periodic_gaps or min(periodic_gaps) < 50 or max(periodic_gaps) > 100:
                    result["errors"].append(f"{role}: 50 ms periodic timing outside [50,100]: {periodic_gaps}")
                if role == "bms" and (not dt_gaps or min(dt_gaps) < 10):
                    result["errors"].append(f"{role}: TP gaps below 10 ms: {dt_gaps}")
                result.setdefault("timing_ms", {})[role] = dict(dt_gaps=dt_gaps, periodic_gaps=periodic_gaps)
            if case["expect"] == 0:
                transitions = re.findall(r"to=(\w+) reason=", text)
                expected = ["identify", "config", "ready", "charging", "stopping", "statistics", "done"]
                position = 0
                for state in transitions:
                    if position < len(expected) and state == expected[position]:
                        position += 1
                if position != len(expected):
                    result["errors"].append(f"{role}: incomplete/out-of-order states {transitions}")
        for token in case.get("any_evidence", []):
            if not any(token in text for text in texts.values()):
                result["errors"].append(f"missing combined evidence: {token}")
        if not result["errors"]:
            result["status"] = "PASS"
        result["exit_codes"] = {role: process.returncode for role, process in processes.items()}
    except (OSError, subprocess.TimeoutExpired) as error:
        result["errors"].append(str(error))
    finally:
        for process in processes.values():
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for handle in handles:
            handle.close()
    result["evidence_directory"] = str(directory)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", help="仅运行指定场景")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--real-time", action="store_true")
    args = parser.parse_args()
    build = (ROOT / args.build_dir).resolve()
    output = build / "scenarios"
    output.mkdir(parents=True, exist_ok=True)
    cases = scenarios()
    if args.case:
        cases = [case for case in cases if case["name"] == args.case]
        if not cases:
            parser.error("unknown scenario")
    try:
        socket.if_nametoindex("vcan0")
    except OSError:
        results = [dict(name=case["name"], status="SKIP", reason="vcan0 unavailable") for case in cases]
        (output / "report.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        print("SKIP: vcan0 unavailable")
        return 77
    speed = 1 if args.real_time else int(os.environ.get("SIM_SPEED", "10"))
    if not 1 <= speed <= 100:
        parser.error("SIM_SPEED must be 1..100")
    results = []
    for case in cases:
        result = run_case(case, build / "gbt27930_simulator", output / case["name"], speed)
        results.append(result)
        print(f"{result['status']}: {case['name']} {'; '.join(result['errors'])}", flush=True)
    (output / "report.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    return int(any(result["status"] == "FAIL" for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
