#!/usr/bin/env python3
"""Test-only RFB 3.8 client over TCP/WebSocket; no deployed Python dependencies."""
import base64
import hashlib
import os
import socket
import struct


class WebVncClient:
    def __init__(self, port, websocket=True):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=4)
        self.websocket = websocket
        self.buffer = bytearray()
        if websocket:
            key = base64.b64encode(os.urandom(16)).decode()
            self.sock.sendall((f'GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n'
                               f'Connection: Upgrade\r\nOrigin: http://127.0.0.1\r\nSec-WebSocket-Key: {key}\r\n'
                               'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: binary\r\n\r\n').encode())
            header = bytearray()
            while not header.endswith(b'\r\n\r\n'):
                header.extend(self._wire(1))
                assert len(header) < 8192
            accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
            assert b'101' in header.split(b'\r\n')[0] and accept in header, header
        assert self.read(12) == b'RFB 003.008\n'
        self.send(b'RFB 003.008\n')
        count = self.read(1)[0]
        assert count and 1 in self.read(count)
        self.send(b'\x01')
        assert self.read(4) == bytes(4)
        self.send(b'\x01')
        init = self.read(24)
        self.width, self.height = struct.unpack('!HH', init[:4])
        self.name = self.read(struct.unpack('!I', init[20:])[0]).decode()
        # Force RAW XRGB8888 for independent deterministic pixel assertions.
        pixel = struct.pack('!BBBBHHHBBBxxx', 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
        self.send(bytes(4) + pixel)
        self.send(b'\x02\x00\x00\x01' + struct.pack('!i', 0))

    def _wire(self, length):
        data = bytearray()
        while len(data) < length:
            block = self.sock.recv(length-len(data))
            if not block:
                raise EOFError('VNC connection closed')
            data.extend(block)
        return bytes(data)

    def _frame(self, payload, opcode=2):
        key = os.urandom(4)
        size = len(payload)
        header = bytes((0x80 | opcode, 0x80 | size)) if size < 126 else bytes((0x80 | opcode, 0xFE)) + struct.pack('!H', size)
        self.sock.sendall(header + key + bytes(value ^ key[idx % 4] for idx, value in enumerate(payload)))

    def send(self, payload):
        if self.websocket:
            self._frame(payload)
        else:
            self.sock.sendall(payload)

    def read(self, length):
        if not self.websocket:
            return self._wire(length)
        while len(self.buffer) < length:
            first, second = self._wire(2)
            size = second & 127
            if size == 126:
                size = struct.unpack('!H', self._wire(2))[0]
            elif size == 127:
                size = struct.unpack('!Q', self._wire(8))[0]
            assert not second & 128 and size <= 16*1024*1024
            payload = self._wire(size)
            opcode = first & 15
            if opcode in (0, 2):
                self.buffer.extend(payload)
            elif opcode == 9:
                self._frame(payload, 10)
            elif opcode == 8:
                raise EOFError('WebSocket closed')
        result = bytes(self.buffer[:length])
        del self.buffer[:length]
        return result

    def pointer(self, x, y, pressed):
        self.send(struct.pack('!BBHH', 5, int(pressed), x, y))

    def click(self, x, y):
        self.pointer(x, y, True)
        self.pointer(x, y, False)

    def image(self):
        self.send(struct.pack('!BBHHHH', 3, 0, 0, 0, self.width, self.height))
        assert self.read(1) == b'\x00'
        _, count = struct.unpack('!BH', self.read(3))
        assert count
        image = bytearray(self.width*self.height*4)
        for _ in range(count):
            x, y, width, height, encoding = struct.unpack('!HHHHi', self.read(12))
            assert encoding == 0 and x+width <= self.width and y+height <= self.height
            pixels = self.read(width*height*4)
            for row in range(height):
                offset = ((y+row)*self.width+x)*4
                image[offset:offset+width*4] = pixels[row*width*4:(row+1)*width*4]
        return bytes(image)

    def close(self):
        self.sock.close()


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]
