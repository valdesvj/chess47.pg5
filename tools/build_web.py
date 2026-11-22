#!/usr/bin/env python3
"""build_web.py - web/chess47.html, Chess 47 and Xiangqi 47 in one file: web/app.html with web/engine.js and
web/xq_engine.js (the engines, also the Web Worker's), the bitmaps of src/pieces.h, src/letters.h and
src/xq_pieces.h, and web/app.js. GPL-3.0-or-later."""
import json, os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rd = lambda p: open(os.path.join(ROOT, p), encoding='utf-8').read()


def blocks20(h):
    bl = re.findall(r'\{\s*/\*\s*(\w+)\s*\*/((?:\s*"[^"]*",?)+)\s*\}', h)
    return [[]] + [re.findall(r'"([^"]*)"', b) for _, b in bl]


h = rd('src/pieces.h')
pieces = blocks20(h)[:7]
crazy = re.findall(r'"([^"]*)"', h[h.index('CRAZY_BMP'):])
letters = blocks20(rd('src/letters.h'))
assert len(pieces) == 7 and all(len(p) == 20 for p in pieces[1:]) and len(crazy) == 22, 'pieces.h'
assert len(letters) == 7 and all(len(p) == 20 for p in letters[1:]), 'letters.h'

# src/xq_pieces.h: rows of 3 bytes -> 24-bit numbers (bit 23 the left pixel)
x = rd('src/xq_pieces.h')
def arr(name):
    body = x[x.index(name + '['):]
    body = body[body.index('{'):body.index(';')]
    return [int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', body)]
def rows(b):
    return [b[i] << 16 | b[i + 1] << 8 | b[i + 2] for i in range(0, len(b), 3)]
def pieces_of(name, size):
    b = arr(name)
    n = size * 3
    assert len(b) == 3 * 2 * 7 * n, name
    return [[[rows(b[((s * 2 + c) * 7 + t) * n:((s * 2 + c) * 7 + t + 1) * n]) for t in range(7)] for c in range(2)]
            for s in range(3)]
z = int(re.search(r'#define XQ_PSIZE (\d+)', x).group(1))
nn = int(re.search(r'#define XQ_PSIZE_N (\d+)', x).group(1))
rv = arr('XQ_RIVER')
sv = arr('XQ_SEVEN')
xq = {'size': z, 'sizeN': nn, 'pieces': pieces_of('XQ_PIECE', z), 'piecesN': pieces_of('XQ_PIECE_N', nn),
      'disc': rows(arr('XQ_DISC')), 'discN': rows(arr('XQ_DISC_N')),
      'river': [[rv[k * 30 + 2 * y] << 8 | rv[k * 30 + 2 * y + 1] for y in range(15)] for k in range(4)],
      'seven': [[sv[k * 30 + 2 * y] << 8 | sv[k * 30 + 2 * y + 1] for y in range(15)] for k in range(4)]}

# src/puzzles.h: Puzzles 47
pzs = [{'fen': f, 'kind': k, 'n': int(n), 'title': t, 'line': l} for f, k, n, t, l in
       re.findall(r'\{\s*"([^"]+)",\s*PZ_(\w+),\s*(\d+),\s*"([^"]*)",\s*"([^"]*)"\s*\}', rd('src/puzzles.h'))]
assert len(pzs) == 47, 'puzzles.h'

bmp = ('window.CHESS47_BMP = ' + json.dumps({'pieces': pieces, 'crazy': crazy, 'letters': letters}) + ';\n' +
       'window.XQ_BMP = ' + json.dumps(xq, separators=(',', ':')) + ';\n' +
       'window.PUZZLES47 = ' + json.dumps(pzs) + ';')
html = rd('web/app.html').replace('/*ENGINE*/', rd('web/engine.js') + '\n' + rd('web/xq_engine.js'))
html = html.replace('/*PIECES*/', bmp).replace('/*APP*/', rd('web/app.js'))
open(os.path.join(ROOT, 'web', 'chess47.html'), 'w', encoding='utf-8').write(html)
print('web/chess47.html', len(html), 'bytes')
