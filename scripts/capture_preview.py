#!/usr/bin/env python3
"""Render the game's actual ANSI demo output into a PNG (requires Pillow)."""
import argparse
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import struct
import subprocess
import termios
import time
import unicodedata

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent


def font_path(*candidates):
    return next(str(Path(p)) for p in candidates if Path(p).is_file())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compact', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/gameplay.png')
    args = parser.parse_args()
    width, height = (80, 24) if args.compact else (96, 32)
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', height, width, 0, 0))
    env = dict(os.environ)
    env.pop('NO_COLOR', None)
    process = subprocess.Popen([str(ROOT / 'burger'), '--demo'],
                               stdout=slave, stderr=slave, env=env)
    os.close(slave)
    data = bytearray()
    try:
        until = time.monotonic() + 5
        while time.monotonic() < until:
            if select.select([master], [], [], 0.05)[0]:
                try:
                    chunk = os.read(master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not chunk:
                    break
                data.extend(chunk)
        process.wait(timeout=2)
        if process.returncode:
            raise RuntimeError(data.decode(errors='replace'))
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
    frame = bytes(data).split(b'\x1b[H')[-1].decode('utf-8')
    cell_w, cell_h = 12, 26
    image = Image.new('RGB', (width * cell_w + 48, height * cell_h + 80), '#14191d')
    draw = ImageDraw.Draw(image)
    mono_path = font_path('/mnt/c/Windows/Fonts/consola.ttf',
                          '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf')
    korean_path = font_path('/mnt/c/Windows/Fonts/malgun.ttf',
                            '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc', mono_path)
    mono = ImageFont.truetype(mono_path, 20)
    korean = ImageFont.truetype(korean_path, 19)
    for i, color in enumerate(('#ff675d', '#ffbf42', '#35c84b')):
        draw.ellipse((24 + i * 22, 17, 36 + i * 22, 29), fill=color)
    draw.text((110, 14), 'BURGER STACK  /  v1.1  /  GAME DEMO', fill='#8c9b99',
              font=ImageFont.truetype(mono_path, 16))
    fg, bg = (240, 230, 205), (20, 25, 29)
    x = y = 0
    for token in re.split(r'(\x1b\[[0-?]*[ -/]*[@-~])', frame):
        if token.startswith('\x1b['):
            if token.endswith('m'):
                values = [int(v) for v in token[2:-1].split(';') if v.isdigit()]
                if len(values) == 5 and values[:2] == [38, 2]:
                    fg = tuple(values[2:])
                elif len(values) == 5 and values[:2] == [48, 2]:
                    bg = tuple(values[2:])
            continue
        for char in token:
            if char == '\r':
                x = 0
                continue
            if char == '\n':
                x = 0
                y += 1
                continue
            if y >= height:
                break
            columns = 2 if unicodedata.east_asian_width(char) in ('W', 'F') else 1
            left, top = 24 + x * cell_w, 52 + y * cell_h
            draw.rectangle((left, top, left + columns * cell_w - 1, top + cell_h - 1), fill=bg)
            if char == '▀':
                draw.rectangle((left, top, left + cell_w - 1, top + cell_h // 2 - 1), fill=fg)
            elif char == '━':
                draw.rectangle((left, top + 12, left + cell_w - 1, top + 14), fill=fg)
            elif char != ' ':
                draw.text((left, top + 1), char, fill=fg, font=korean if columns == 2 else mono)
            x += columns
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output)
    print(args.output)


if __name__ == '__main__':
    main()
