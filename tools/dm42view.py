#!/usr/bin/env python3
"""dm42view.py - the DM42n's screen in your terminal, sent over USB by NAV47 (>> >> USB) or Chess 47 (>> >> USB).

    python3 tools/dm42view.py                     # /dev/ttyACM0, Braille dots: 200 x 60 characters
    python3 tools/dm42view.py --zoom 0,0,200,104  # a part of the screen, bigger (x, y, width, height)
    python3 tools/dm42view.py --blocks            # half blocks: 1 character = 1 x 2 pixels (needs 400 columns)
    python3 tools/dm42view.py --png shots/        # also save every screen as a PNG (needs Pillow)
    python3 tools/dm42view.py --once --png .      # one screen, then exit

The frames are text: "NAV47 LCD 400 240" (or "CHESS47 LCD ..."), 240 lines of 100 hex digits (1 = ink), "END".
On Linux the user needs access to the port: sudo usermod -aG uucp $USER (Arch) or dialout (Debian), then log in again.
"""
import argparse
import os
import shutil
import sys
import time

W, H = 400, 240


def frames(path):
    """The frames from the serial port (or a file of them)."""
    if os.path.exists(path) and not path.startswith('/dev/'):
        f = open(path, 'rb')
    else:
        import termios
        fd = os.open(path, os.O_RDONLY | os.O_NOCTTY)
        t = termios.tcgetattr(fd)
        t[0] = 0; t[1] = 0; t[3] = 0                    # raw: no echo, no line editing, no translation
        t[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(fd, termios.TCSANOW, t)
        f = os.fdopen(fd, 'rb', buffering=0)
    rows, inside = [], False
    buf = b''
    while True:
        chunk = f.read(4096)
        if not chunk:
            if not path.startswith('/dev/'):
                return
            time.sleep(0.01)
            continue
        buf += chunk
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            line = line.strip().decode('ascii', 'replace')
            if line.endswith('LCD 400 240'):
                rows, inside = [], True
            elif line == 'END' and inside:
                inside = False
                if len(rows) == H:
                    yield rows
            elif inside and len(line) == W // 4:
                v = int(line, 16)
                rows.append([(v >> (W - 1 - x)) & 1 for x in range(W)])


def braille(px, x0, y0, w, h, scale):
    """Unicode Braille: 2 x 4 dots a character; scale > 1 repeats each pixel."""
    out = []
    sw, sh = w * scale, h * scale
    get = lambda x, y: px[y0 + y // scale][x0 + x // scale] if 0 <= y < sh and 0 <= x < sw else 0
    bits = ((0, 0, 0x01), (0, 1, 0x02), (0, 2, 0x04), (1, 0, 0x08), (1, 1, 0x10), (1, 2, 0x20), (0, 3, 0x40), (1, 3, 0x80))
    for cy in range(0, sh, 4):
        out.append(''.join(chr(0x2800 + sum(m for dx, dy, m in bits if get(cx + dx, cy + dy)))
                           for cx in range(0, sw, 2)))
    return out


def blocks(px, x0, y0, w, h):
    ch = {(0, 0): ' ', (1, 0): '\u2580', (0, 1): '\u2584', (1, 1): '\u2588'}
    return [''.join(ch[(px[y][x], px[y + 1][x] if y + 1 < y0 + h else 0)] for x in range(x0, x0 + w))
            for y in range(y0, y0 + h, 2)]


def main():
    ap = argparse.ArgumentParser(description='The DM42n screen (NAV47 / Chess 47 USB) in the terminal.')
    ap.add_argument('port', nargs='?', default='/dev/ttyACM0', help='serial port, or a file of frames')
    ap.add_argument('--zoom', help='x,y,w,h: show only this part, as big as the terminal allows')
    ap.add_argument('--blocks', action='store_true', help='half blocks instead of Braille dots')
    ap.add_argument('--png', help='save every frame as a PNG in this folder')
    ap.add_argument('--scale', type=int, default=3, help='PNG pixel size (default 3)')
    ap.add_argument('--once', action='store_true', help='one frame, then exit')
    a = ap.parse_args()
    x0, y0, w, h = map(int, a.zoom.split(',')) if a.zoom else (0, 0, W, H)
    n = 0
    try:
        for px in frames(a.port):
            n += 1
            cols, lines = shutil.get_terminal_size((200, 62))
            if a.blocks:
                art = blocks(px, x0, y0, w, h)
            else:
                scale = max(1, min((cols * 2) // w, ((lines - 2) * 4) // h))
                art = braille(px, x0, y0, w, h, scale)
            sys.stdout.write('\x1b[H\x1b[2J' + '\n'.join(art) + '\n' + time.strftime('%H:%M:%S') + '  frame %d' % n + '\n')
            sys.stdout.flush()
            if a.png:
                from PIL import Image
                os.makedirs(a.png, exist_ok=True)
                img = Image.new('RGB', (W, H), (223, 230, 214))
                p = img.load()
                for y in range(H):
                    for x in range(W):
                        if px[y][x]:
                            p[x, y] = (30, 34, 32)
                img = img.resize((W * a.scale, H * a.scale), Image.NEAREST)
                name = os.path.join(a.png, time.strftime('dm42_%Y%m%d_%H%M%S') + '_%03d.png' % n)
                img.save(name)
            if a.once:
                break
    except KeyboardInterrupt:
        pass
    except PermissionError:
        sys.exit('no access to %s: add yourself to the uucp (Arch) or dialout (Debian) group' % a.port)
    except FileNotFoundError:
        sys.exit('%s not found: on the DM42n press >> >> USB in NAV47 or Chess 47 (the USB cable connected)' % a.port)


if __name__ == '__main__':
    main()
