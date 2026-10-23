#!/usr/bin/env python3
"""将固定 noVNC 模块及入口网页嵌入 C 数据，构建和运行均无需下载资源。"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
assets = [('index.vnc', root / 'app/ui/web_index.html')]
for directory in ('core', 'vendor'):
    assets.extend((path.relative_to(root / 'third_party/novnc').as_posix(), path)
                  for path in sorted((root / 'third_party/novnc' / directory).rglob('*.js')))
output = Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
with output.open('w', encoding='utf-8', newline='\n') as stream:
    stream.write('/* Generated from pinned, licensed assets; do not edit. */\n')
    for index, (_, path) in enumerate(assets):
        data = path.read_bytes()
        stream.write(f'static const unsigned char ui_web_asset_{index}[] = {{\n')
        for offset in range(0, len(data), 24):
            stream.write(','.join(str(byte) for byte in data[offset:offset+24]) + ',\n')
        stream.write('};\n')
    stream.write('static const ui_web_asset_t ui_web_assets[] = {\n')
    for index, (name, _) in enumerate(assets):
        stream.write(f'{{"/{name}", ui_web_asset_{index}, sizeof(ui_web_asset_{index})}},\n')
    stream.write('};\n')
