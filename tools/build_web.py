#!/usr/bin/env python3
"""build_web.py - web/chess47.html, Chess 47 in one file: web/app.html with web/engine.js (the engine, also the
Web Worker's), the piece bitmaps of src/pieces.h and web/app.js. GNU GPLv3."""
import json, os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rd = lambda p: open(os.path.join(ROOT, p), encoding='utf-8').read()
h = rd('src/pieces.h')
blocks = re.findall(r'\{\s*/\*\s*(\w+)\s*\*/((?:\s*"[^"]*",?)+)\s*\}', h)
pieces = [[]] + [re.findall(r'"([^"]*)"', b) for _, b in blocks]
crazy = re.findall(r'"([^"]*)"', h[h.index('CRAZY_BMP'):])
assert len(pieces) == 7 and all(len(p) == 20 for p in pieces[1:]) and len(crazy) == 22, 'pieces.h'
bmp = 'window.CHESS47_BMP = ' + json.dumps({'pieces': pieces, 'crazy': crazy}) + ';'
html = rd('web/app.html').replace('/*ENGINE*/', rd('web/engine.js')).replace('/*PIECES*/', bmp).replace('/*APP*/', rd('web/app.js'))
open(os.path.join(ROOT, 'web', 'chess47.html'), 'w', encoding='utf-8').write(html)
print('web/chess47.html', len(html), 'bytes')
