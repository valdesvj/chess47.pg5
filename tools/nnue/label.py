#!/usr/bin/env python3
"""
NNUE training data, step 2: Stockfish's (chess) or Pikafish's (Xiangqi) score of each position of tools/nnue/gen.c.

  python3 tools/nnue/label.py chess IN OUT [--depth 9] [--jobs 14]
  python3 tools/nnue/label.py xq IN OUT --engine ~/opt/Pikafish/src/pikafish

IN: FEN|result lines (shuffled already, so the order of OUT does not matter); OUT: FEN|result|score (centipawns from White's / Red's side, mates as +-20000).
Each worker drives one engine process (1 thread, 16 MB hash) over plain UCI.
(C) 2026 Victor Valdes - GPL-3.0-or-later
"""
import argparse
import os
import subprocess
import sys
from multiprocessing import Pool

A = None


def init(a):
    global A, E
    A = a
    E = subprocess.Popen([a.engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1,
                         cwd=os.path.dirname(a.engine) or '.')
    send('uci'); until('uciok')
    send('setoption name Threads value 1'); send('setoption name Hash value 16')
    send('isready'); until('readyok')


def send(s):
    E.stdin.write(s + '\n'); E.stdin.flush()


def until(w):
    while True:
        l = E.stdout.readline()
        if not l:
            raise RuntimeError('the engine stopped')
        if l.startswith(w):
            return l


def score(line):
    fen, res = line.rstrip('\n').split('|')
    send('position fen ' + fen)
    send('go depth %d nodes %d' % (A.depth, A.nodes))   # the node cap: no search runs away
    sc = None
    while True:
        l = E.stdout.readline()
        if not l:
            raise RuntimeError('the engine stopped')
        if l.startswith('info') and ' score ' in l:
            t = l.split()
            i = t.index('score')
            if t[i + 1] == 'cp':
                sc = int(t[i + 2])
            elif t[i + 1] == 'mate':
                m = int(t[i + 2])
                sc = 20000 if m > 0 else -20000
        elif l.startswith('bestmove'):
            break
    if sc is None:
        return None
    if fen.split()[1] == 'b':
        sc = -sc
    return '%s|%s|%d\n' % (fen, res, sc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', choices=['chess', 'xq'])
    ap.add_argument('inp'); ap.add_argument('out')
    ap.add_argument('--engine', default='')
    ap.add_argument('--depth', type=int, default=9)
    ap.add_argument('--nodes', type=int, default=200000, help='at most this many nodes a position')
    ap.add_argument('--skip', type=int, default=0, help='skip the first lines (resume; OUT is appended to)')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 2))
    a = ap.parse_args()
    a.engine = os.path.expanduser(a.engine or ('/usr/bin/stockfish' if a.game == 'chess'
                                               else '~/opt/Pikafish/src/pikafish'))
    lines = open(a.inp).readlines()[a.skip:]
    n = 0
    with Pool(a.jobs, initializer=init, initargs=(a,)) as pool, open(a.out, 'a' if a.skip else 'w') as o:
        for r in pool.imap_unordered(score, lines, chunksize=64):     # in any order: a slow one holds none
            if r:
                o.write(r)
            n += 1
            if n % 100000 == 0:
                print('  %d / %d' % (n, len(lines)), file=sys.stderr, flush=True)
    print('%d positions labelled' % n, file=sys.stderr)


if __name__ == '__main__':
    main()
