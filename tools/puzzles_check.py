#!/usr/bin/env python3
"""puzzles_check.py - checks Puzzles 47 (src/puzzles.h) with python-chess and Stockfish. GPL-3.0-or-later.

  python3 tools/puzzles_check.py [--engine stockfish] [N ...]      (or: make puzzle-test)

For each problem: the position legal, the stored line legal, and
  MATE n   a mate in exactly n: the first move the only one that mates in n or less, the line ends in mate;
  WIN      the first move the only one that wins (+2.50 or more), every other move below +2.00;
  DRAW     the first move the only one that does not lose (above -1.00);
  and (WIN / DRAW) every solver's move of the line keeps the result.
"""
import argparse
import os
import re
import sys

import chess
import chess.engine

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIN, NEAR, LOSE = 250, 200, -100


def read_puzzles():
    h = open(os.path.join(ROOT, 'src', 'puzzles.h'), encoding='utf-8').read()
    pat = re.compile(r'\{\s*"([^"]+)",\s*PZ_(\w+),\s*(\d+),\s*"([^"]*)",\s*"([^"]*)"\s*\}')
    return [(fen, kind, int(n), title, line.split()) for fen, kind, n, title, line in pat.findall(h)]


def cp(score):
    return score.score(mate_score=100000)


def check(sf, i, fen, kind, n, title, line):
    errs = []
    b = chess.Board(fen)
    if not b.is_valid():
        return [f'illegal position ({b.status()})']
    t = b.copy()
    for u in line:
        m = chess.Move.from_uci(u)
        if m not in t.legal_moves:
            return [f'illegal move {u} in the line']
        t.push(m)
    nl = b.legal_moves.count()
    infos = sf.analyse(b, chess.engine.Limit(depth=34 if kind == 'MATE' and n >= 4 else 26, time=60), multipv=nl)
    res = [(inf['pv'][0], inf['score'].pov(b.turn)) for inf in infos]
    key = chess.Move.from_uci(line[0])
    if kind == 'MATE':
        mates = [(m, s.mate()) for m, s in res if s.mate() is not None and 0 < s.mate() <= n]
        if [m for m, _ in mates] != [key]:
            errs.append('the moves that mate in %d: %s' % (n, ' '.join(b.san(m) for m, _ in mates) or 'none'))
        elif mates[0][1] != n:
            errs.append(f'a shorter mate: {mates[0][1]}')
        if len(line) != 2 * n - 1 or not t.is_checkmate():
            errs.append('the line does not end in mate in %d' % n)
    else:
        if kind == 'WIN':
            good = [m for m, s in res if cp(s) >= WIN]
            near = [m for m, s in res if NEAR <= cp(s) < WIN]
        else:
            good = [m for m, s in res if cp(s) > LOSE]
            near = []
        if good != [key] or near:
            errs.append('the moves that %s: %s' % (kind.lower(), ' '.join(b.san(m) for m in good + near) or 'none'))
        t = b.copy()
        for k, u in enumerate(line):
            t.push(chess.Move.from_uci(u))
            if k % 2 == 0 and not t.is_game_over():
                s = sf.analyse(t, chess.engine.Limit(depth=22, time=10))['score'].pov(b.turn)
                if (kind == 'WIN' and cp(s) < WIN) or (kind == 'DRAW' and cp(s) <= LOSE):
                    errs.append(f'the line\'s move {k // 2 + 1} ({u}) does not keep the result: {s}')
    return errs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine', default='stockfish')
    ap.add_argument('only', nargs='*', type=int)
    a = ap.parse_args()
    P = read_puzzles()
    if len(P) != 47:
        print(f'{len(P)} puzzles, not 47'); return 1
    sf = chess.engine.SimpleEngine.popen_uci(a.engine)
    sf.configure({'Threads': max(1, (os.cpu_count() or 2) - 2), 'Hash': 512})
    bad = 0
    for i, p in enumerate(P, 1):
        if a.only and i not in a.only:
            continue
        errs = check(sf, i, *p)
        print(f'{i:2d} {"ok " if not errs else "BAD"} {p[1]}{p[2] or ""}  {p[3]}' + ''.join('\n     ' + e for e in errs))
        bad += bool(errs)
    sf.quit()
    print('all good' if not bad else f'{bad} bad')
    return 1 if bad else 0


sys.exit(main())
