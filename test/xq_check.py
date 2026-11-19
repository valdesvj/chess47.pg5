#!/usr/bin/env python3
"""xq_check.py - the Xiangqi engine against Fairy-Stockfish (pyffish: pip install pyffish):
  1. perft 1-3 on positions reached by random games (checks, cannon screens, blocked horses and elephants, facing
     generals, soldiers over the river);
  2. the WXF notation of every legal move of those positions (test/xq_wxf.c prints the engine's).
    make xq-test        (or: python3 test/xq_check.py [positions])
(C) 2026 Victor Valdes - GPL-3.0-or-later"""
import os, random, subprocess, sys
import pyffish as sf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
V = 'xiangqi'


def positions(n, seed=47):
    rnd = random.Random(seed)
    out = []
    while len(out) < n:
        fen, moves = sf.start_fen(V), []
        for ply in range(rnd.randint(4, 90)):
            legal = sf.legal_moves(V, fen, moves)
            if not legal:
                break
            caps = [m for m in legal if sf.is_capture(V, fen, moves, m)]
            moves.append(rnd.choice(caps) if caps and rnd.random() < 0.4 else rnd.choice(legal))
        out.append(sf.get_fen(V, fen, moves))
    return out


def perft(fen, d, moves=()):
    ms = sf.legal_moves(V, fen, list(moves))
    if d == 1:
        return len(ms)
    return sum(perft(fen, d - 1, list(moves) + [m]) for m in ms)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    fens = positions(n)
    exe = os.path.join(ROOT, 'build', 'xq_perft')
    jobs = [(d, f) for i, f in enumerate(fens) for d in ((1, 2, 3) if i < 15 else (1, 2))]
    r = subprocess.run([exe, '-'], input=''.join('%d %s\n' % j for j in jobs), text=True, capture_output=True)
    got = r.stdout.split()
    bad = 0
    for (d, f), g in zip(jobs, got):
        w = perft(f, d)
        if str(w) != g:
            bad += 1
            if bad <= 10:
                print('PERFT FAIL d%d %s: engine %s, Fairy-Stockfish %d' % (d, f, g, w))
    print('perft: %d checks on %d positions, %d wrong' % (len(jobs), len(fens), bad))
    # WXF
    exe = os.path.join(ROOT, 'build', 'xq_wxf')
    r = subprocess.run([exe], input=''.join(f + '\n' for f in fens), text=True, capture_output=True)
    wbad = wn = 0
    for line in r.stdout.splitlines():
        fen, mv, wxf = line.rsplit(' ', 2)
        mv1 = mv[0] + str(int(mv[1:-2].rstrip('abcdefghi') or 0)) if False else None
        want = sf.get_san(V, fen, mv, False, sf.NOTATION_XIANGQI_WXF)
        wn += 1
        if want != wxf:
            wbad += 1
            if wbad <= 10:
                print('WXF FAIL %s %s: engine %s, Fairy-Stockfish %s' % (fen, mv, wxf, want))
    print('WXF: %d moves, %d different' % (wn, wbad))
    print('ALL PASS' if not bad and not wbad else 'FAILED')
    sys.exit(1 if bad or wbad else 0)


if __name__ == '__main__':
    main()
