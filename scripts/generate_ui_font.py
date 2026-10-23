#!/usr/bin/env python3
"""使用 LVGL 官方 lv_font_conv 1.5.3 生成界面专用思源黑体字形。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
FONT_SHA256 = '84bbd4ace91d327b3ad1a581c688196278a4e41308520176f419180064e4af2b'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, required=True)
    parser.add_argument('--converter', type=Path, required=True)
    args = parser.parse_args()
    version = json.loads((args.converter.resolve().parent / 'package.json').read_text(encoding='utf-8'))
    if version.get('name') != 'lv_font_conv' or version.get('version') != '1.5.3':
        parser.error('Use the official lv_font_conv 1.5.3')
    if hashlib.sha256(args.font.read_bytes()).hexdigest() != FONT_SHA256:
        parser.error('SourceHanSansSC-Regular.otf must be the official 2.004R snapshot')
    text = (ROOT / 'app/ui/sim_ui_view.c').read_text(encoding='utf-8')
    symbols = ''.join(sorted({char for char in text if ord(char) > 127}))
    directory = ROOT / 'third_party/source_han_sans'
    (directory / 'ui_chars.txt').write_text(symbols + '\n', encoding='utf-8', newline='\n')
    subprocess.run(['node', str(args.converter.resolve()), '--size', '16', '--bpp', '4',
                    '--format', 'lvgl', '--no-compress', '--no-kerning', '--font', str(args.font.resolve()),
                    '-r', '0x20-0x7e', '--symbols', symbols, '--lv-fallback', 'lv_font_montserrat_14',
                    '--lv-font-name', 'sim_ui_font_16', '-o', str(directory / 'sim_ui_font_16.c')], check=True)
    output = directory / 'sim_ui_font_16.c'
    data = output.read_text(encoding='utf-8')
    # Keep the generated source independent of workstation paths.
    data = '\n'.join(' * Opts: See scripts/generate_ui_font.py; Source Han Sans SC 2.004R, lv_font_conv 1.5.3' if line.startswith(' * Opts:') else line for line in data.splitlines()) + '\n'
    output.write_text(data, encoding='utf-8', newline='\n')
    print(f'Generated {len(symbols)} non-ASCII glyphs plus printable ASCII')


if __name__ == '__main__':
    main()
