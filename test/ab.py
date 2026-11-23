#!/usr/bin/env python3
"""
A/B test of two builds of the engine (chess or Xiangqi), games in parallel on the PC's cores.

  python3 test/ab.py build/chess47-uci build/chess47-uci-base --nodes 25000 --games 400
  ~/opt/venv-xq/bin/python test/ab.py --xq build/xq-uci build/xq-uci-base --nodes 25000 --games 400
  python3 test/ab.py NEW OLD --movetime 200          (equal time: the faster build gains)

Each opening (random plies, seeded) is played twice, each build with both colours. The hash table is the
calculator's (C47_HASH_KB=1024). Prints NEW's W/D/L, its score and the Elo difference with the 95% margin.
Chess: python-chess is the referee; Xiangqi: pyffish (Fairy-Stockfish's rules), as test/xq_match.py.
(C) 2026 Victor Valdes - GPL-3.0-or-later
"""
import argparse
import math
import os
import random
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed


class Uci:
    def __init__(self, cmd, env, level=None):
        self.p = subprocess.Popen([cmd, '-q'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1,
                                  env=env)
        self.send('uci'); self.until('uciok')
        if level is not None:
            self.send('setoption name Level value %d' % level)

    def send(self, s):
        self.p.stdin.write(s + '\n'); self.p.stdin.flush()

    def until(self, w):
        while True:
            l = self.p.stdout.readline()
            if not l:
                raise RuntimeError('an engine stopped')
            if l.startswith(w):
                return l.strip()

    def new(self):
        self.send('ucinewgame'); self.send('isready'); self.until('readyok')

    def move(self, fen, moves, go):
        self.send('position %s%s' % (fen, ' moves ' + ' '.join(moves) if moves else ''))
        self.send(go)
        return self.until('bestmove').split()[1]

    def quit(self):
        try:
            self.send('quit'); self.p.wait(5)
        except Exception:
            self.p.kill()


def opening(xq, seed, plies):
    rng = random.Random(seed)
    if xq:
        import pyffish as sf
        mv = []
        for _ in range(plies):
            mv.append(rng.choice(sorted(sf.legal_moves('xiangqi', sf.start_fen('xiangqi'), mv))))
        return mv
    import chess
    b = chess.Board()
    while True:
        b.reset()
        for _ in range(plies):
            b.push(rng.choice(sorted(b.legal_moves, key=lambda m: m.uci())))
        if not b.is_game_over():
            return [m.uci() for m in b.move_stack]


def result_chess(moves, eng, go):
    """eng[0] plays White; 1 / 0.5 / 0 for White"""
    import chess
    b = chess.Board()
    for m in moves:
        b.push_uci(m)
    while not b.is_game_over(claim_draw=True) and b.ply() < 400:
        m = eng[b.ply() % 2].move('startpos', [x.uci() for x in b.move_stack], go)
        mv = chess.Move.from_uci(m)
        if mv not in b.legal_moves:
            return 0.0 if b.turn == chess.WHITE else 1.0
        b.push(mv)
    r = b.result(claim_draw=True)
    return 1.0 if r == '1-0' else 0.0 if r == '0-1' else 0.5


def result_xq(moves, eng, go):
    import pyffish as sf
    V = 'xiangqi'
    start = sf.start_fen(V)
    moves = list(moves)
    # the referee sees the position after the last capture and the moves since (a capture ends any repetition or
    # chase): pyffish replays its move list at every call
    base, tail = start, list(moves)

    def men(fen):
        return sum(ch.isalpha() for ch in fen.split()[0])
    while True:
        legal = sf.legal_moves(V, base, tail)
        if not legal:
            r = sf.game_result(V, base, tail); break
        end, r = sf.is_immediate_game_end(V, base, tail)
        if end:
            break
        end, r = sf.is_optional_game_end(V, base, tail)
        if end:
            break
        if len(moves) >= 300:
            r = 0; break
        m = eng[len(moves) % 2].move('startpos', moves, go)
        if m not in legal:
            return 0.0 if len(moves) % 2 == 0 else 1.0
        moves.append(m)
        fen = sf.get_fen(V, base, tail + [m])
        if men(fen) < men(sf.get_fen(V, base, tail)):
            base, tail = fen, []
        else:
            tail.append(m)
    if r == 0:
        return 0.5
    red_to_move = len(moves) % 2 == 0
    return 1.0 if (r > 0) == red_to_move else 0.0


def pair(a, k):
    """two games from opening k: NEW's points"""
    env = dict(os.environ, C47_HASH_KB=str(a.hash))
    new, old = Uci(a.new, env, a.level), Uci(a.old, env, a.level)
    go = 'go movetime %d' % a.movetime if a.movetime else 'go nodes %d' % a.nodes
    mv = opening(a.xq, a.seed * 100000 + k, a.plies)
    pts = []
    try:
        for new_first in (1, 0):
            for e in (new, old):
                e.new()
            eng = (new, old) if new_first else (old, new)
            r = (result_xq if a.xq else result_chess)(mv, eng, go)
            pts.append(r if new_first else 1 - r)
    finally:
        new.quit(); old.quit()
    return pts


def elo(s):
    s = min(max(s, 1e-3), 1 - 1e-3)
    return -400 * math.log10(1 / s - 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('new'); ap.add_argument('old')
    ap.add_argument('--xq', action='store_true', help='Xiangqi (the engines are xq-uci builds; needs pyffish)')
    ap.add_argument('--nodes', type=int, default=25000)
    ap.add_argument('--movetime', type=int, default=0, help='ms a move instead of nodes')
    ap.add_argument('--games', type=int, default=200)
    ap.add_argument('--plies', type=int, default=0, help='random opening plies (default: chess 8, Xiangqi 6)')
    ap.add_argument('--hash', type=int, default=1024, help='KB of hash table (the calculator: 1024)')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument('--seed', type=int, default=47)
    ap.add_argument('--level', type=int, default=None, help='both engines at this level (1-9: its depth and noise)')
    a = ap.parse_args()
    a.plies = a.plies or (6 if a.xq else 8)
    pts = []
    with ProcessPoolExecutor(a.jobs) as ex:
        fs = [ex.submit(pair, a, k) for k in range(a.games // 2)]
        for i, f in enumerate(as_completed(fs), 1):
            pts += f.result()
            if i % 20 == 0:
                print('  %d games: %.1f%%' % (len(pts), 100 * sum(pts) / len(pts)), file=sys.stderr, flush=True)
    n = len(pts)
    w, d, l = pts.count(1.0), pts.count(0.5), pts.count(0.0)
    s = sum(pts) / n
    var = sum((p - s) ** 2 for p in pts) / n
    m = 1.96 * math.sqrt(var / n)
    los = 0.5 * (1 + math.erf((w - l) / math.sqrt(2 * (w + l)))) if w + l else 0.5
    print('%s %s vs %s, %s: +%d =%d -%d  %.1f%%  Elo %+.0f (95%%: %+.0f .. %+.0f)  LOS %.0f%%' % (
        'xiangqi' if a.xq else 'chess', a.new, a.old,
        ('level %d, ' % a.level if a.level is not None else '') + ('movetime %d' % a.movetime if a.movetime else 'nodes %d' % a.nodes),
        w, d, l, 100 * s, elo(s), elo(s - m), elo(s + m), 100 * los), flush=True)


if __name__ == '__main__':
    main()
