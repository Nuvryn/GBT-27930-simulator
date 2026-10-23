#!/usr/bin/env python3
"""核验固定依赖内容及清单完整性，不忽略额外文件。"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parent.parent
    manifest = json.loads((root / "third_party/manifests/dependencies-manifest.json").read_text(encoding="utf-8"))
    errors = []
    for component in manifest["components"]:
        directory = root / component["directory"]
        actual = {path.relative_to(directory).as_posix(): path for path in directory.rglob("*") if path.is_file()}
        expected = {entry["path"]: entry["sha256_lf"] for entry in component["files"]}
        for path, digest in expected.items():
            if path not in actual or hashlib.sha256(actual[path].read_bytes().replace(b"\r\n", b"\n")).hexdigest() != digest:
                errors.append(f"{component['name']}: missing/modified {path}")
        for path in actual.keys() - expected.keys():
            errors.append(f"{component['name']}: unregistered {path}")
    for error in errors:
        print(error)
    if errors:
        return 1
    print("PASS: " + ", ".join(component["name"] for component in manifest["components"]) + " snapshots")
    return subprocess.run([sys.executable, str(root / "scripts/check_open_sae_j1939.py")], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
