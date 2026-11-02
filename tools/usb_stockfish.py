#!/usr/bin/env python3
"""
usb_stockfish.py - let Stockfish (on Linux) play against the DM42n over USB.

The calculator sends its moves, Stockfish answers here, and you type
Stockfish's reply on the calculator keypad (Chess 47 does not read the port yet).
The screen frames Chess 47 also sends (for tools/dm42view.py) are skipped.

Setup on the DM42n:  Chess 47 -> MODE "2 players", menu page 3 -> USB (ON).
On Linux:            python3 tools/usb_stockfish.py [/dev/ttyACM0] [--elo 2000] [--time 2]

The calculator sends lines like:
    newgame <FEN>          (on NEW / 960 / LOAD)
    e2e4                   (every move, castling as king->rook square)
"""
import argparse
import sys

import chess
import chess.engine


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", default="/dev/ttyACM0")
    ap.add_argument("--engine", default="stockfish")
    ap.add_argument("--elo", type=int, default=0, help="limit Stockfish strength (1320-3190), 0 = full")
    ap.add_argument("--time", type=float, default=2.0, help="seconds per Stockfish move")
    ap.add_argument("--side", choices=["white", "black"], default="black", help="colour Stockfish plays")
    a = ap.parse_args()

    eng = chess.engine.SimpleEngine.popen_uci(a.engine)
    if a.elo:
        eng.configure({"UCI_LimitStrength": True, "UCI_Elo": a.elo})
    sf_white = a.side == "white"
    board = chess.Board()

    def think():
        if board.is_game_over():
            print("Game over:", board.result())
            return
        if (board.turn == chess.WHITE) != sf_white:
            return
        r = eng.play(board, chess.engine.Limit(time=a.time))
        san = board.san(r.move)
        print(f"\n>>> Stockfish plays {san}  ({r.move.uci()})  - enter it on the DM42n\n", flush=True)
        board.push(r.move)

    print(f"Listening on {a.port} ... (Ctrl+C to quit)")
    frame = False
    with open(a.port, "r", errors="ignore") as port:
        for raw in port:
            line = raw.strip()
            if not line:
                continue
            if line.endswith("LCD 400 240"):    # a screen frame (tools/dm42view.py): skip to its END
                frame = True
                continue
            if frame:
                frame = line != "END"
                continue
            if line.startswith("result"):
                print(line)
                continue
            if line.startswith("newgame"):
                fen = line[len("newgame"):].strip()
                board = chess.Board(fen, chess960=True)
                print("New game:", fen)
                think()
                continue
            if line.startswith("CHESS47"):
                print("Calculator connected.")
                continue
            try:
                mv = chess.Move.from_uci(line)
                # the calculator writes castling king->rook; python-chess accepts it in chess960 mode
                if mv not in board.legal_moves:
                    print("Ignoring", line)
                    continue
                if (board.turn == chess.WHITE) == sf_white:
                    board.push(mv)              # it was the Stockfish move you just entered
                    continue
                print("DM42n:", board.san(mv))
                board.push(mv)
                think()
            except ValueError:
                print("?", line)
    eng.quit()


if __name__ == "__main__":
    sys.exit(main())
