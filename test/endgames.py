#!/usr/bin/env python3
"""
The endgames Chess 47 must win: random won positions of king and pawn against king (src/kpk.h says won), and king
and queen, rook, or bishop and knight against king; Chess 47 (a UCI build, test/uci.c) on the winning side, Stockfish
defending.
Won = checkmate before the 50-move rule or a repetition.

  python3 test/endgames.py build/chess47-uci [--nodes 50000] [--count 20] [--stockfish /usr/bin/stockfish]
(C) 2026 Victor Valdes - GPL-3.0-or-later
"""
import argparse
import os
import random
import re
import sys
from concurrent.futures import ThreadPoolExecutor

import chess
import chess.engine

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
BITS = [int(x) for x in re.search(r'\{([^}]*)\}', open(os.path.join(ROOT, 'src', 'kpk.h')).read())
        .group(1).replace('\n', '').split(',')]


def kpk_won(b):
    """the table, for the pawn's side"""
    pawn = next(iter(b.pieces(chess.PAWN, chess.WHITE) | b.pieces(chess.PAWN, chess.BLACK)))
    strong = b.color_at(pawn)
    wk, bk, ps, stm = b.king(strong), b.king(not strong), pawn, 0 if b.turn == strong else 1
    if strong == chess.BLACK:
        wk, bk, ps = wk ^ 56, bk ^ 56, ps ^ 56
    if ps & 7 > 3:
        wk, bk, ps = wk ^ 7, bk ^ 7, ps ^ 7
    i = ((stm * 24 + ((ps >> 3) - 1) * 4 + (ps & 7)) * 64 + wk) * 64 + bk
    return (BITS[i >> 3] >> (i & 7)) & 1


def positions(kind, n, rng):
    out = []
    while len(out) < n:
        col = rng.choice([chess.WHITE, chess.BLACK])
        b = chess.Board(None)
        pieces = {'kpk': [chess.PAWN], 'kbnk': [chess.BISHOP, chess.KNIGHT], 'kqk': [chess.QUEEN], 'krk': [chess.ROOK]}[kind]
        sq = rng.sample(range(64), 2 + len(pieces))
        b.set_piece_at(sq[0], chess.Piece(chess.KING, col))
        b.set_piece_at(sq[1], chess.Piece(chess.KING, not col))
        for s, t in zip(sq[2:], pieces):
            b.set_piece_at(s, chess.Piece(t, col))
        b.turn = col                                   # the winning side to move
        if kind == 'kpk' and chess.square_rank(sq[2]) in (0, 7):
            continue
        if not b.is_valid() or b.is_game_over():
            continue
        if kind == 'kpk' and (not kpk_won(b) or chess.square_rank(sq[2]) not in ((1, 2, 3) if col else (4, 5, 6))):
            continue                                   # won, and the pawn still far from queening
        out.append((kind, b.fen(), col))
    return out


def play(a, job):
    kind, fen, col = job
    us = chess.engine.SimpleEngine.popen_uci(a.engine)
    sf = chess.engine.SimpleEngine.popen_uci(a.stockfish)
    try:
        b = chess.Board(fen)
        while not b.is_game_over(claim_draw=True):
            if b.turn == col:
                m = us.play(b, chess.engine.Limit(nodes=a.nodes)).move
            else:
                m = sf.play(b, chess.engine.Limit(depth=12)).move
            b.push(m)
        won = b.is_checkmate() and b.turn != col
        return kind, fen, won, len(b.move_stack)
    finally:
        us.quit(); sf.quit()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('engine')
    ap.add_argument('--nodes', type=int, default=50000)
    ap.add_argument('--count', type=int, default=20)
    ap.add_argument('--stockfish', default='/usr/bin/stockfish')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument('--seed', type=int, default=47)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    res = {'kpk': [], 'kbnk': [], 'kqk': [], 'krk': []}
    jobs = [j for k in res for j in positions(k, a.count, rng)]
    with ThreadPoolExecutor(a.jobs) as ex:
        for kind, fen, won, plies in ex.map(lambda j: play(a, j), jobs):
            res[kind].append(won)
            if not won:
                print('  not won:', kind, fen, '(%d plies)' % plies, flush=True)
    for k, v in res.items():
        print('%s %s: %d / %d won' % (a.engine, k.upper(), sum(v), len(v)))
    sys.exit(0 if all(all(v) for v in res.values()) else 1)


if __name__ == '__main__':
    main()
