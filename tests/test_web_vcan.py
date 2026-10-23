#!/usr/bin/env python3
"""Production Web UI and real-time SocketCAN: two sessions for both versions."""
import json
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from web_vnc_client import WebVncClient, free_port
from test_ui_vcan import live_text
from test_web_ui import wait_for


def main():
    binary = Path(sys.argv[1]).resolve()
    with socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW) as probe:
        try:
            probe.bind(('vcan0',))
        except OSError:
            print('SKIP: vcan0 unavailable')
            return 77
    directory = Path(tempfile.mkdtemp(prefix='web_vcan_', dir=binary.parent))
    report = []
    for version in (2015, 2023):
        processes, logs, ports = {}, {}, {}
        client = None
        try:
            for role in ('bms', 'charger'):
                name = f'{version}_{role}'
                logs[role] = directory / f'{name}.log'
                ports[role] = (free_port(), free_port())
                config = {'role': role, 'version': version, 'start_mode': 'manual', 'can_interface': 'vcan0',
                          'file_log': True, 'log_path': str(logs[role]), 'stop_after_ms': 1000 if role == 'charger' else 0,
                          'max_session_ms': 15000, 'recovery_wait_ms': 100,
                          'ui': {'enabled': True, 'backend': 'web', 'web': {'listen': '127.0.0.1',
                                 'port': ports[role][0], 'vnc_port': ports[role][1]}}}
                path = directory / f'{name}.json'
                path.write_text(json.dumps(config))
                with (directory / f'{name}.console').open('w') as output:
                    processes[role] = subprocess.Popen([str(binary), '--config', str(path)], stdout=output, stderr=subprocess.STDOUT)
            wait_for(lambda: all('READY' in live_text(directory/f'{version}_{role}.console') for role in processes))
            client = WebVncClient(ports['charger'][1])
            assert client.image()
            client.click(400, 123)  # Third tab, charge controls on the 800x480 virtual display.
            time.sleep(0.2)
            client.click(71, 228)
            wait_for(lambda: 'SESSION_CONFIG_END id=1' in live_text(logs['charger']))
            # Closing the browser must not stop either protocol process.
            client.close()
            client = None
            wait_for(lambda: all('id=1 status=PASS' in live_text(path) for path in logs.values()), 15)
            time.sleep(0.3)
            client = WebVncClient(ports['charger'][1])
            assert client.image()
            # Wait for the rendered, enabled Start button before sending the next click.
            # Protocol completion and the UI's 100 ms refresh have separate schedules.
            def start_visible():
                frame = client.image()
                offset = (220*client.width+30)*4
                return frame if frame[offset:offset+3] == bytes((243, 150, 33)) else None
            frame = wait_for(start_visible)
            (directory / f'{version}_reconnect.bgra').write_bytes(frame)
            client.click(71, 228)
            wait_for(lambda: all('id=2 status=PASS' in live_text(path) for path in logs.values()), 15)
            for process in processes.values():
                process.send_signal(signal.SIGTERM)
            for process in processes.values():
                assert process.wait(timeout=5) == 0
            for role, path in logs.items():
                text = live_text(path)
                assert len(re.findall(r'^SESSION_RESULT\s+.*status=PASS', text, re.M)) == 2 and 'status=FAIL' not in text and 'lost data' not in text
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
            report.append({'version': version, 'status': 'PASS', 'sessions': 2})
        finally:
            if client:
                client.close()
            for process in processes.values():
                if process.poll() is None:
                    process.kill()
                process.wait()
    (directory/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({'status': 'PASS', 'evidence': str(directory), 'cases': report}, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
