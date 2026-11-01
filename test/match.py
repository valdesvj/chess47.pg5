#!/usr/bin/env python3
"""
Chess 47 vs Stockfish (UCI_LimitStrength) - Elo bracket test.

  python3 test/match.py --nodes 100000 --elo 1600 --games 20 [--960]

Chess 47 gets a node budget per move (emulates the DM42n speed: ~10k nodes/s on
battery, ~19k on USB power), Stockfish plays at a fixed UCI_Elo with 100 ms/move.
Prints W/D/L and the Elo difference estimate.
"""
import argparse
import math
import random
import sys

import chess
import chess.engine

OPENINGS = [
    "", "e2e4 e7e5", "e2e4 c7c5", "d2d4 d7d5", "d2d4 g8f6", "c2c4 e7e5", "e2e4 e7e6",
    "e2e4 c7c6", "g1f3 d7d5", "d2d4 g8f6 c2c4 e7e6", "e2e4 e7e5 g1f3 b8c6", "e2e4 d7d6",
]


def play(c47, sf, board, c47_white, nodes, sf_ms):
    while not board.is_game_over(claim_draw=True) and board.fullmove_number < 200:
        mine = (board.turn == chess.WHITE) == c47_white
        if mine:
            r = c47.play(board, chess.engine.Limit(nodes=nodes))
        else:
            r = sf.play(board, chess.engine.Limit(time=sf_ms / 1000))
        board.push(r.move)
    res = board.result(claim_draw=True)
    if res == "1/2-1/2" or res == "*":
        return 0.5
    white_won = res == "1-0"
    return 1.0 if white_won == c47_white else 0.0


def elo_diff(score):
    score = min(max(score, 0.01), 0.99)
    return -400 * math.log10(1 / score - 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nodes", type=int, default=100000)
    ap.add_argument("--elo", type=int, default=1600)
    ap.add_argument("--games", type=int, default=20)
    ap.add_argument("--sfms", type=int, default=100)
    ap.add_argument("--960", dest="c960", action="store_true")
    ap.add_argument("--engine", default="build/chess47-uci")
    ap.add_argument("--seed", type=int, default=47)
    a = ap.parse_args()

    c47 = chess.engine.SimpleEngine.popen_uci([a.engine, "-q"])
    sf = chess.engine.SimpleEngine.popen_uci("/usr/games/stockfish")
    sf.configure({"UCI_LimitStrength": True, "UCI_Elo": a.elo, "Threads": 1, "Hash": 16})
    # python-chess sends UCI_Chess960 itself when the board is a 960 board

    w = d = l = 0
    for g in range(a.games):
        pair = g // 2
        if a.c960:
            board = chess.Board.from_chess960_pos(random.Random(a.seed * 1000 + pair).randint(0, 959))
        else:
            board = chess.Board()
            for mv in OPENINGS[pair % len(OPENINGS)].split():
                board.push_uci(mv)
        c47_white = g % 2 == 0
        s = play(c47, sf, board, c47_white, a.nodes, a.sfms)
        w += s == 1.0
        d += s == 0.5
        l += s == 0.0
        print(f"game {g+1:2d} {'W' if c47_white else 'B'} {s}", file=sys.stderr, flush=True)
    c47.quit()
    sf.quit()
    sc = (w + 0.5 * d) / a.games
    print(f"{'960' if a.c960 else 'std'} nodes={a.nodes:>7} vs SF{a.elo}: +{w} ={d} -{l}  "
          f"score {sc*100:.0f}%  => Chess47 ~ {a.elo + elo_diff(sc):.0f}", flush=True)


if __name__ == "__main__":
    main()
