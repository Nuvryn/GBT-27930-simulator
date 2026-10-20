#!/usr/bin/env python3
"""易读视图使用独立字节向量，保留非法输入并转义 HTML。"""
import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("read_can_capture", ROOT / "scripts/read_can_capture.py")
READER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(READER)


def main():
    source = "\n".join([
        "(1792493428.400069) vcan0 1826F456#010100",
        "(1792493428.402581) vcan0 182756F4#4C1D",
        "(1792493428.403736) vcan0 1CEC56F4#1031000701000200",
        "(1792493428.404568) vcan0 1CECF456#110101FFFF000200",
        "(1792493428.404894) vcan0 1CEB56F4#0101010003E803A0",
        "(1792493428.500000) vcan0 181056F4#6810B80B02",
        "<script>alert(1)</script>",
    ])
    rows = READER.decode_lines(source)
    assert len(rows) == 7
    assert "CHM" in rows[0][4] and "协议版本 1.1.0" in rows[0][8]
    assert rows[1][2] == "2.512 ms" and "750 V" in rows[1][8]
    assert "BMS(F4) → 充电桩(56)" == rows[2][3]
    assert "BRM · 总长49字节 · 7包" in rows[2][8]
    assert "CTS" in rows[3][8] and "第1包" in rows[3][8]
    assert "第1个数据包" in rows[4][8]
    assert "420 V" in rows[5][8] and "100 A" in rows[5][8] and "恒流" in rows[5][8]
    assert rows[6][4] == "未解析"
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "readable.html"
        READER.write_report(output, rows)
        content = output.read_text(encoding="utf-8")
        assert "&lt;script&gt;alert(1)&lt;/script&gt;" in content
        assert "<script>alert(1)</script>" not in content
        assert "420 V" in output.with_suffix(".txt").read_text(encoding="utf-8")
    print("PASS: timestamps, role/PGN, TP controls, physical values and escaped raw evidence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
