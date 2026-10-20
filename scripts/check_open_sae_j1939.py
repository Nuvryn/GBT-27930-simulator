#!/usr/bin/env python3
"""校验原样导入的 Open-SAE-J1939 源码快照（仅统一 CRLF/LF）。"""
import hashlib
import json
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent.parent
    manifest = json.loads((root / "third_party/manifests/open-sae-j1939-manifest.json").read_text(encoding="utf-8"))
    vendor = root / "third_party/open_sae_j1939"
    expected = {entry["path"]: entry["sha256_lf"] for entry in manifest["files"]}
    actual = {path.relative_to(vendor).as_posix(): path for path in vendor.rglob("*") if path.is_file()}
    errors = []
    for name, digest in expected.items():
        if name not in actual:
            errors.append(f"缺失文件：{name}")
        elif hashlib.sha256(actual[name].read_bytes().replace(b"\r\n", b"\n")).hexdigest() != digest:
            errors.append(f"内容已变更：{name}")
    for name in sorted(actual.keys() - expected.keys()):
        errors.append(f"未登记文件：{name}")
    for error in errors:
        print(error)
    if not errors:
        print(f"PASS: {len(expected)} 个上游文件与固定快照一致")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
