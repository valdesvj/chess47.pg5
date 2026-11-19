#!/usr/bin/env python3
"""
Xiangqi 47 vs Fairy-Stockfish (UCI_LimitStrength) - Elo bracket test, as test/match.py for chess.

  python3 test/xq_match.py --fairy ~/opt/Fairy-Stockfish/src/stockfish --nodes 100000 --elo 1600 --games 20

Xiangqi 47 (build/xq-uci) gets a node budget per move (the DM42n speed: ~10k nodes/s on battery, ~19k on USB
power), Fairy-Stockfish plays at a fixed UCI_Elo with 100 ms a move. The referee is pyffish (Fairy-Stockfish's
own rules): checkmate, stalemate (a loss), repetition, perpetual check and chasing, the move limit. Games start
from varied openings, each played with both colours. Prints W/D/L and the Elo estimate.

Needs pyffish (on Arch in a venv: python -m venv ~/opt/venv-xq && ~/opt/venv-xq/bin/pip install pyffish) and a
Fairy-Stockfish binary built with largeboards=yes (Xiangqi's board is 9 x 10).
(C) 2026 Victor Valdes - GPL-3.0-or-later
"""
import argparse
import math
import random
import shutil
import subprocess
import sys

import pyffish as sf

V = 'xiangqi'
# first moves, Fairy-Stockfish's coordinates (ranks 1-10): central cannon, same-direction cannons, the elephant,
# the horse and the pawn openings, and Black's usual answers
OPENINGS = [
    '', 'h3e3 h10g8', 'h3e3 h8e8', 'b3e3 b10c8', 'c1e3 h8e8', 'g1e3 b10c8', 'h1g3 h10g8', 'c4c5 b10c8',
    'g4g5 h8e8', 'h3e3 h10g8 h1g3 i10h10', 'b3e3 h8e8', 'h3e3 b10c8 h1g3 a10b10',
]


class Uci:
    def __init__(self, cmd):
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.send('uci')
        self.until('uciok')

    def send(self, s):
        self.p.stdin.write(s + '\n')
        self.p.stdin.flush()

    def until(self, word):
        while True:
            l = self.p.stdout.readline()
            if not l:
                raise SystemExit('an engine stopped')
            if l.startswith(word):
                return l.strip()

    def option(self, name, value):
        self.send('setoption name %s value %s' % (name, value))

    def ready(self):
        self.send('isready')
        self.until('readyok')

    def move(self, moves, go):
        self.send('position startpos' + (' moves ' + ' '.join(moves) if moves else ''))
        self.send(go)
        return self.until('bestmove').split()[1]

    def quit(self):
        try:
            self.send('quit')
            self.p.wait(5)
        except Exception:
            self.p.kill()


def play(x47, fsf, opening, x47_red, nodes, fsf_go, max_plies):
    """1 / 0.5 / 0 for Xiangqi 47"""
    start = sf.start_fen(V)
    moves = opening.split() if opening else []
    for e in (x47, fsf):
        e.send('ucinewgame')
        e.ready()
    while True:
        legal = sf.legal_moves(V, start, moves)
        if not legal:
            r = sf.game_result(V, start, moves)
            break
        end, r = sf.is_immediate_game_end(V, start, moves)
        if end:
            break
        end, r = sf.is_optional_game_end(V, start, moves)      # repetition, perpetual check / chasing, move limit
        if end:
            break
        if len(moves) >= max_plies:
            r = 0
            break
        red_to_move = len(moves) % 2 == 0
        mine = red_to_move == x47_red
        mv = x47.move(moves, 'go nodes %d' % nodes) if mine else fsf.move(moves, fsf_go)
        if mv not in legal:                                     # an illegal move loses
            print('  illegal move %s by %s after %s' % (mv, 'Xiangqi 47' if mine else 'Fairy-Stockfish',
                  ' '.join(moves)), file=sys.stderr)
            return 0.0 if mine else 1.0
        moves.append(mv)
    # r: the result for the side to move (> 0 it wins, < 0 it loses, 0 a draw)
    if r == 0:
        return 0.5
    stm_wins = r > 0
    red_to_move = len(moves) % 2 == 0
    red_wins = stm_wins == red_to_move
    return 1.0 if red_wins == x47_red else 0.0


def elo_diff(score):
    score = min(max(score, 0.01), 0.99)
    return -400 * math.log10(1 / score - 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nodes', type=int, default=100000)
    ap.add_argument('--elo', type=int, default=1600, help="Fairy-Stockfish's UCI_Elo")
    ap.add_argument('--games', type=int, default=20)
    ap.add_argument('--sfms', type=int, default=100, help="Fairy-Stockfish's time a move (ms)")
    ap.add_argument('--engine', default='build/xq-uci')
    ap.add_argument('--level', type=int, default=-1, help='Xiangqi 47 level 1-9 (default: full strength = level 9)')
    ap.add_argument('--fairy', default=shutil.which('fairy-stockfish') or 'fairy-stockfish',
                    help='the Fairy-Stockfish binary (built with largeboards=yes)')
    ap.add_argument('--full', action='store_true', help='Fairy-Stockfish at full strength (no UCI_Elo)')
    ap.add_argument('--plies', type=int, default=300, help='a draw after this many plies')
    ap.add_argument('--opp-nodes', type=int, default=0, help='the opponent searches this many nodes a move (an A/B '
                    'test of two builds of the engine: --fairy build/xq-uci-old --full --opp-nodes N)')
    ap.add_argument('--random', type=int, default=0, help='openings of this many random plies (a pair each, seeded)')
    ap.add_argument('--seed', type=int, default=47)
    ap.add_argument('--first', type=int, default=0, help='start at this game (repeat one part of a match)')
    a = ap.parse_args()

    for o in OPENINGS:                                          # the openings must be legal
        mv = o.split() if o else []
        for i in range(len(mv)):
            assert mv[i] in sf.legal_moves(V, sf.start_fen(V), mv[:i]), o

    x47 = Uci([a.engine, '-q'])
    if 1 <= a.level <= 9:
        x47.option('Level', a.level)
    x47.ready()
    fsf = Uci([a.fairy])
    fsf.option('UCI_Variant', V)
    fsf.option('Threads', 1)
    fsf.option('Hash', 16)
    if not a.full:
        fsf.option('UCI_LimitStrength', 'true')
        fsf.option('UCI_Elo', a.elo)
    fsf.ready()

    w = d = l = 0
    for g in range(a.first, a.games):
        x47_red = g % 2 == 0
        if a.random:                                            # the same random opening for both colours
            rng, mv = random.Random(a.seed * 1000 + g // 2), []
            for _ in range(a.random):
                mv.append(rng.choice(sf.legal_moves(V, sf.start_fen(V), mv)))
            op = ' '.join(mv)
        else:
            op = OPENINGS[(g // 2) % len(OPENINGS)]
        fsf_go = 'go nodes %d' % a.opp_nodes if a.opp_nodes else 'go movetime %d' % a.sfms
        s = play(x47, fsf, op, x47_red, a.nodes, fsf_go, a.plies)
        w += s == 1.0
        d += s == 0.5
        l += s == 0.0
        print(f"game {g+1:2d} {'Red  ' if x47_red else 'Black'} {s}", file=sys.stderr, flush=True)
    x47.quit()
    fsf.quit()
    a.games -= a.first
    sc = (w + 0.5 * d) / a.games
    var = (w * (1 - sc) ** 2 + d * (0.5 - sc) ** 2 + l * sc ** 2) / a.games
    m = 1.96 * math.sqrt(var / a.games)
    lo, hi = elo_diff(sc - m), elo_diff(sc + m)
    who = 'FSF full' if a.full else f'FSF{a.elo}'
    print(f"xiangqi nodes={a.nodes:>7} vs {who}: +{w} ={d} -{l}  score {sc*100:.0f}%" +
          ('' if a.full else f"  => Xiangqi47 ~ {a.elo + elo_diff(sc):.0f}  (95%: {a.elo + lo:.0f} .. {a.elo + hi:.0f})"),
          flush=True)


if __name__ == '__main__':
    main()
