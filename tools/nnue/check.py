#!/usr/bin/env python3
"""The evaluation network: numpy (the header's int16 weights, as the C) against test/nnue_check.c on FENs.
  python3 tools/nnue/check.py chess|xq HEADER FENFILE [N]       (C: build/nnue_check, built with that header)
(C) 2026 Victor Valdes - GPL-3.0-or-later"""
import os, re, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from train import features

game, hdr, fens = sys.argv[1], sys.argv[2], sys.argv[3]
n = int(sys.argv[4]) if len(sys.argv) > 4 else 300
h = open(hdr).read()
G = 'NNC' if game == 'chess' else 'NNX'
num = lambda k: int(re.search(r'#define %s_%s (\d+)' % (G, k), h).group(1))
arr = lambda k: np.array([int(x) for x in re.search(r'%s_%s\[[^]]*\] = \{([^}]*)\}' % (G, k), h, re.S).group(1).replace('\n', '').split(',')], np.int64)
IN, HID, QA, QB, SC = num('IN'), num('HID'), num('QA'), num('QB'), num('SCALE')
W1, B1, W2 = arr('W1').reshape(IN, HID), arr('B1'), arr('W2')
B2 = int(re.search(r'%s_B2 = (-?\d+)' % G, h).group(1))
lines = [l.split('|')[0] for l in open(fens)][:n]
want = []
for fen in lines:
    w, b, stm = features(game, fen)
    aw, ab = B1 + W1[w].sum(0), B1 + W1[b].sum(0)
    us, them = (aw, ab) if stm == 0 else (ab, aw)
    out = B2 + (np.clip(us, 0, QA) * W2[:HID]).sum() + (np.clip(them, 0, QA) * W2[HID:]).sum()
    want.append(int(np.trunc(out * SC / (QA * QB))))
got = subprocess.run(['build/nnue_check'] + (['xq'] if game == 'xq' else []), input='\n'.join(lines) + '\n',
                     capture_output=True, text=True).stdout.split()
bad = sum(str(a) != b for a, b in zip(want, got)) + abs(len(got) - len(want))
print('%d positions, %d different%s' % (len(want), bad, '' if not bad else ': ' + ' '.join(got[-3:])))
sys.exit(1 if bad else 0)
