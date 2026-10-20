#!/usr/bin/env python3
"""将 candump -L 文件转换为可搜索 HTML 和易读文本，不修改原始证据。"""
import argparse
from datetime import datetime
from decimal import Decimal
from html import escape
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
MESSAGE_LABELS = {
    "CHM": "桩握手", "BHM": "BMS握手", "CRM": "桩辨识", "BRM": "BMS辨识",
    "BCP": "电池充电参数", "CTS": "时间同步", "CML": "桩输出能力", "BRO": "BMS准备状态",
    "CRO": "桩准备状态", "BCL": "电池充电需求", "BCS": "电池充电总状态", "CCS": "桩充电状态",
    "BSM": "电池状态信息", "BMV": "单体电压", "BMT": "单体温度", "BSP": "电池预留信息",
    "BST": "BMS中止", "CST": "桩中止", "BSD": "BMS统计", "CSD": "桩统计",
    "BEM": "BMS错误", "CEM": "桩错误",
}
FIELD_LABELS = {
    "bhm_max_voltage_mv": "最高允许电压", "crm_result": "辨识结果", "charger_id": "桩编号",
    "bro_ready": "BMS就绪", "cro_ready": "桩就绪", "bcl_voltage_mv": "需求电压",
    "bcl_current_ma": "需求电流幅值", "bcl_mode": "充电模式", "ccs_voltage_mv": "输出电压",
    "ccs_current_ma": "输出电流幅值", "ccs_elapsed_minutes": "累计充电时间", "ccs_allow": "允许充电",
    "cml_max_voltage_mv": "最高电压", "cml_min_voltage_mv": "最低电压",
    "cml_max_current_ma": "最大电流幅值", "cml_min_current_ma": "最小电流幅值",
}


def load_schema():
    source = (ROOT / "app/protocol/gbt27930_codec.c").read_text(encoding="utf-8")
    descriptors = re.findall(r'\{"(\w+)",\s*(0x[0-9A-Fa-f]+)u,\s*\d+u,\s*\d+u,', source)
    messages = {int(pgn, 16): name for name, pgn in descriptors}
    fields = {}
    for key, message, numbers, required in re.findall(
            r'GBT27930_F\("([^"]+)",\s*(\w+),\s*([\d\s,\-L]+),\s*(true|false)\)', source):
        values = [int(value.strip().rstrip("L")) for value in numbers.split(",")]
        fields.setdefault(message, []).append((key, *values, required == "true"))
    if len(messages) != 22 or not fields:
        raise ValueError("无法读取工程报文/字段注册表")
    return messages, fields


def summarize(message, data, fields):
    if message == "CHM" and len(data) >= 3:
        return "协议版本 " + ".".join(map(str, data[:3]))
    result = []
    packed = int.from_bytes(data, "little")
    for key, bit, width, scale, offset, minimum, maximum, initial, required in fields.get(message, []):
        if bit + width > len(data) * 8:
            continue
        raw = (packed >> bit) & ((1 << width) - 1)
        label = FIELD_LABELS.get(key, key)
        if raw == (1 << width) - 1 and not required:
            result.append(f"{label}=不可用")
            continue
        if message == "CTS":
            digits = f"{raw:X}"
            if any(digit not in "0123456789" for digit in digits):
                result.append(f"{label}=非法BCD")
                continue
            raw = int(digits)
        value = raw * scale + offset
        unit = ""
        if key.endswith("_mv"):
            value, unit = f"{value / 1000:g}", " V"
        elif key.endswith("_ma"):
            value, unit = f"{value / 1000:g}", " A"
        elif key.endswith("_minutes"):
            unit = " min"
        elif key.endswith("_pct"):
            unit = "%"
        elif key.endswith("_degc"):
            unit = " ℃"
        if key in ("bro_ready", "cro_ready", "crm_result"):
            value = {0: "否", 170: "是"}.get(raw, f"非法({raw})")
        elif key == "bcl_mode":
            value = {1: "恒压", 2: "恒流"}.get(raw, f"非法({raw})")
        result.append(f"{label}={value}{unit}")
        if raw * scale + offset < minimum or raw * scale + offset > maximum:
            result[-1] += "（超出字段范围）"
    return "；".join(result)


def decode_lines(text, charger=0x56, bms=0xF4):
    messages, fields = load_schema()
    roles = {charger: "充电桩", bms: "BMS", 255: "广播"}
    rows = []
    previous = None
    for line_number, line in enumerate(text.splitlines(), 1):
        match = re.fullmatch(r"\((\d+\.\d+)\)\s+(\S+)\s+([0-9A-Fa-f]{3,8})#([0-9A-Fa-f]*)", line)
        if match is None:
            rows.append([str(line_number), "—", "—", "—", "未解析", "—", "—", line, "保留原文：非经典CAN数据帧或抓包诊断"])
            continue
        stamp_text, interface, id_text, hex_data = match.groups()
        stamp = Decimal(stamp_text)
        identifier = int(id_text, 16)
        try:
            data = bytes.fromhex(hex_data)
            wall_time = datetime.fromtimestamp(int(stamp)).strftime("%Y-%m-%d %H:%M:%S") + "." + stamp_text.split(".")[1]
        except (ValueError, OverflowError, OSError):
            rows.append([str(line_number), stamp_text, "—", interface, "未解析", "—", id_text, hex_data, "非法时间或字节"])
            continue
        delta = "—" if previous is None else f"{(stamp - previous) * 1000:.3f} ms"
        previous = stamp
        if len(id_text) != 8 or identifier & 0xE0000000:
            rows.append([str(line_number), wall_time, delta, interface, "标准帧/错误帧", "—", id_text, data.hex(" ").upper(), "保留原始CAN标识符"])
            continue
        source, destination = identifier & 255, (identifier >> 8) & 255
        direction = f"{roles.get(source, f'地址{source:02X}')}({source:02X}) → {roles.get(destination, f'地址{destination:02X}')}({destination:02X})"
        pf = (identifier >> 16) & 255
        pgn = (identifier >> 8) & 0x3FFFF
        if pf < 240:
            pgn &= 0x3FF00
        message = messages.get(pgn, "未知PGN")
        summary = summarize(message, data, fields)
        if pgn == 0xEC00:
            message = "TP.CM"
            if len(data) == 8:
                target_pgn = int.from_bytes(data[5:8], "little")
                target = messages.get(target_pgn, f"PGN {target_pgn:06X}")
                control = {16: "RTS 请求发送", 17: "CTS 允许发送", 19: "ACK 传输完成", 255: "ABORT 终止"}.get(data[0], f"控制码{data[0]:02X}")
                summary = f"{control} · {target}"
                if data[0] in (16, 19):
                    summary += f" · 总长{int.from_bytes(data[1:3], 'little')}字节 · {data[3]}包"
                elif data[0] == 17:
                    summary += f" · 允许{data[1]}包 · 从第{data[2]}包开始"
                elif data[0] == 255:
                    summary += f" · 原因码{data[1]}"
            else:
                summary = "TP.CM 长度异常"
        elif pgn == 0xEB00:
            message = "TP.DT"
            summary = ("非法DT序号0" if data[0] == 0 else f"第{data[0]}个数据包 · 后7字节为分包内容") if len(data) == 8 else "TP.DT 长度异常"
        rows.append([str(line_number), wall_time, delta, direction, message + " " + MESSAGE_LABELS.get(message, ""),
                     f"{pgn:06X}", id_text.upper(), data.hex(" ").upper(), summary])
    return rows


def write_report(path, rows):
    headings = ["序号", "真实时间（本地时区）", "帧间隔", "方向", "报文", "PGN", "CAN ID", "原始字节", "字段 / TP说明"]
    body = "\n".join("<tr>" + "".join(f"<td>{escape(cell)}</td>" for cell in row) + "</tr>" for row in rows)
    document = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>GB/T 27930 抓包视图</title>
+<style>body{font:14px system-ui,sans-serif;margin:24px;color:#203047;background:#f5f7fa}h1{font-size:22px}p{color:#526277}input{padding:10px;width:440px;max-width:85%;border:1px solid #bac8d8;border-radius:6px}table{border-collapse:collapse;background:white;width:100%;margin-top:16px}th,td{text-align:left;padding:10px;border-bottom:1px solid #e1e7ef;vertical-align:top}th{background:#e5edf7;position:sticky;top:0}td:nth-child(2),td:nth-child(4),td:nth-child(5),td:nth-child(8){white-space:nowrap}td:nth-child(6),td:nth-child(7),td:nth-child(8){font-family:monospace}tbody tr:hover{background:#edf5ff}.scroll{overflow:auto}footer{margin-top:16px;color:#526277}</style>
+<h1>GB/T 27930 原始报文 · 易读视图</h1><p>共 COUNT 行 · 时间为抓包真实时间 · 字段仅作辅助展示，原始文件保留用于核对</p>
+<input id="filter" placeholder="搜索：BCL、RTS、BMS、CAN ID 或字段值"><span id="count"></span><div class="scroll"><table><thead><tr>HEADINGS</tr></thead><tbody>BODY</tbody></table></div>
+<footer>TP 分包逐帧展示，不重组为应用报文。帧间隔是相邻所有帧的间隔，不是某个报文的发送周期。无效行保留原文。</footer>
+<script>const f=document.querySelector('#filter'), rows=[...document.querySelectorAll('tbody tr')];f.oninput=()=>{let count=0;for(const row of rows){row.hidden=!row.textContent.toLowerCase().includes(f.value.toLowerCase());if(!row.hidden)count++;}document.querySelector('#count').textContent=' 显示 '+count+' 行';};</script></html>'''.replace("\n+", "\n")
    document = document.replace("COUNT", str(len(rows))).replace("HEADINGS", "".join(f"<th>{escape(title)}</th>" for title in headings)).replace("BODY", body)
    path.write_text(document, encoding="utf-8")
    path.with_suffix(".txt").write_text(" | ".join(headings) + "\n" + "\n".join(" | ".join(row) for row in rows) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--charger-address", type=lambda value: int(value, 0), default=0x56)
    parser.add_argument("--bms-address", type=lambda value: int(value, 0), default=0xF4)
    args = parser.parse_args()
    if not 0 <= args.charger_address <= 253 or not 0 <= args.bms_address <= 253 or args.charger_address == args.bms_address:
        parser.error("addresses must be distinct and in 0..253")
    output = args.output or Path(str(args.capture) + ".readable.html")
    if args.capture.resolve() in (output.resolve(), output.with_suffix(".txt").resolve()):
        parser.error("output must not overwrite the raw capture")
    try:
        rows = decode_lines(args.capture.read_text(encoding="utf-8", errors="replace"), args.charger_address, args.bms_address)
        write_report(output, rows)
    except (OSError, ValueError) as error:
        parser.exit(1, f"FAIL: {error}\n")
    print(f"生成 {len(rows)} 行：{output} / {output.with_suffix('.txt')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
