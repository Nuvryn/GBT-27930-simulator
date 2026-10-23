#!/usr/bin/env python3
"""Real HTTP/WebSocket/LVGL input, disconnection cancellation, ports and cleanup."""
import json
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from web_vnc_client import WebVncClient, free_port


def wait_for(check, seconds=5):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(0.02)
    raise AssertionError('condition timed out')


def main():
    binary = Path(sys.argv[1]).resolve()
    evidence = Path(tempfile.mkdtemp(prefix='web_ui_', dir=binary.parent))
    report = []
    for width, height in ((800, 480), (480, 272)):
        http, vnc = free_port(), free_port()
        while vnc == http:
            vnc = free_port()
        log = evidence / f'host_{width}.log'
        clients = []
        with log.open('w') as output:
            process = subprocess.Popen([str(binary), str(http), str(vnc), str(width), str(height)], stdout=output, stderr=subprocess.STDOUT)
        try:
            text = lambda: log.read_text(errors='replace')
            wait_for(lambda: 'READY' in text() or process.poll() is not None)
            assert process.poll() is None, text()
            page = urllib.request.urlopen(f'http://127.0.0.1:{http}/', timeout=3).read().decode()
            assert 'core/rfb.js' in page and f':{vnc}/' in page and '$PORT' not in page
            module = urllib.request.urlopen(f'http://127.0.0.1:{http}/core/rfb.js', timeout=3)
            assert module.headers.get_content_type() == 'application/javascript' and len(module.read()) > 1000
            for path in ('missing.js', 'core/../../../etc/passwd'):
                try:
                    urllib.request.urlopen(f'http://127.0.0.1:{http}/{path}', timeout=3)
                    raise AssertionError('unknown resource was served')
                except urllib.error.HTTPError as error:
                    assert error.code == 404
            client = WebVncClient(vnc)
            clients.append(client)
            assert (client.width, client.height) == (width, height)
            pixels = client.image()
            assert len(set(pixels)) > 16, 'screen is blank'
            (evidence / f'frame_{width}.bgra').write_bytes(pixels)
            targets = {name: (int(x), int(y)) for name, x, y in re.findall(r'TARGET (\w+) (\d+) (\d+)', text())}
            # A second client must not steal the existing connection.
            refused = False
            try:
                second = WebVncClient(vnc)
                clients.append(second)
            except (EOFError, ConnectionError, AssertionError, TimeoutError):
                refused = True
            assert refused
            assert client.image()
            # Disconnect while holding Start: cancellation must never become a click.
            client.pointer(*targets['start'], True)
            wait_for(lambda: 'pressed=1' in text())
            before = len(text())
            client.close()
            clients.remove(client)
            wait_for(lambda: 'pressed=0' in text()[before:])
            assert 'session=1' not in text(), text()
            # Reconnect with a complete frame and perform actual Start and Stop clicks.
            client = WebVncClient(vnc)
            clients.append(client)
            assert client.image()
            client.click(*targets['start'])
            wait_for(lambda: 'session=1 active=1' in text())
            client.click(*targets['stop'])
            wait_for(lambda: 'session=1 active=0' in text())
            if width == 800:
                # An incomplete RFB handshake must release the single-client slot.
                client.close()
                clients.remove(client)
                wait_for(lambda: 'pressed=0' in text())
                time.sleep(0.1)
                with socket.create_connection(('127.0.0.1', vnc), timeout=7) as incomplete:
                    assert incomplete.recv(12) == b'RFB 003.008\n'
                    assert incomplete.recv(1) == b''
                client = WebVncClient(vnc)
                clients.append(client)
                assert client.image()
            invalid = subprocess.run([str(binary), str(free_port()), str(free_port()), str(width), str(height), 'invalid'],
                                     capture_output=True, text=True, timeout=5)
            assert invalid.returncode != 0 and 'OPEN_FAILED' in invalid.stderr
            # The active listeners must reject a second instance, with clean cleanup.
            busy = subprocess.run([str(binary), str(http), str(vnc), str(width), str(height)], capture_output=True, text=True, timeout=5)
            assert busy.returncode != 0 and 'OPEN_FAILED' in busy.stderr
            for connection in clients:
                connection.close()
            clients.clear()
            process.send_signal(signal.SIGTERM)
            assert process.wait(timeout=5) == 0, text()
            report.append({'size': [width, height], 'status': 'PASS'})
        finally:
            for connection in clients:
                connection.close()
            if process.poll() is None:
                process.kill()
            process.wait()
    (evidence/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({'status': 'PASS', 'evidence': str(evidence), 'cases': report}, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
