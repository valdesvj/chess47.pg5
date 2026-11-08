#!/usr/bin/env python3
"""
usb_stockfish.py - Stockfish (on Linux) plays Chess 47 on the DM42n over USB, both ways.

On the DM42n: Chess 47, menu page 3 -> MODE until "You vs USB" (it switches USB on). SIDE chooses your colour.
On Linux:     python3 tools/usb_stockfish.py [port] [--elo 2000] [--time 2]

The calculator sends:   CHESS47 hello / newgame <FEN> / opponent white|black / every move (e2e4) / result ...
                        and the screen (skipped here: tools/dm42view.py shows it)
Over DMCP5 3.58's serial console (the default build) the lines are on channel 2: tools/dm42port.py does that.
This script sends:      Stockfish's move (e7e5) when it is the USB side's turn; the calculator plays it and sends
                        it back, which confirms it. "msg <text>" shows a text on the calculator.
--manual: the old one-way way: Stockfish's move is printed and you type it on the calculator ("2 players" mode).
"""
import argparse
import glob
import os
import shutil
import sys
import termios

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm42port  # noqa: E402

import chess
import chess.engine


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", default=None, help="default: the DM42n in /dev/serial/by-id")
    ap.add_argument("--engine", default=shutil.which("stockfish") or "/usr/games/stockfish")
    ap.add_argument("--elo", type=int, default=0, help="limit Stockfish strength (1320-3190), 0 = full")
    ap.add_argument("--time", type=float, default=2.0, help="seconds per Stockfish move")
    ap.add_argument("--side", choices=["white", "black"], default="black",
                    help="Stockfish's colour until the calculator says (opponent white|black)")
    ap.add_argument("--manual", action="store_true", help="print Stockfish's move, you type it (one-way)")
    a = ap.parse_args()
    if not a.port:
        a.port = dm42port.find()

    eng = chess.engine.SimpleEngine.popen_uci(a.engine)
    if a.elo:
        eng.configure({"UCI_LimitStrength": True, "UCI_Elo": a.elo})
    sf_white = a.side == "white"
    board = chess.Board()
    port = dm42port.Port(a.port)              # DMCP's serial console (channel 2) or the TinyUSB port

    def send(s):
        port.send(s)

    def think():
        if board.is_game_over(claim_draw=True):
            return
        if (board.turn == chess.WHITE) != sf_white:
            return
        r = eng.play(board, chess.engine.Limit(time=a.time))
        san = board.san(r.move)
        if a.manual:
            print(f"\n>>> Stockfish plays {san}  ({r.move.uci()})  - enter it on the DM42n\n", flush=True)
            board.push(r.move)
        else:
            print(f"Stockfish: {san}", flush=True)
            send(r.move.uci())                       # pushed when the calculator sends it back

    print(f"Listening on {a.port} ... (Ctrl+C to quit)")
    frame = False
    try:
        for line in port.lines():
            if not line:
                continue
            if line.endswith("LCD 400 240"):         # a screen frame (tools/dm42view.py): skip to its END
                frame = True
                continue
            if frame:
                frame = line != "END"
                continue
            if line.startswith("CHESS47"):
                print("Calculator connected.")
                continue
            if line.startswith("newgame"):
                board = chess.Board(line[len("newgame"):].strip(), chess960=True)
                print("New game:", board.fen())
                think()
                continue
            if line.startswith("opponent"):
                sf_white = line.split()[-1] == "white"
                print("Stockfish plays", "white" if sf_white else "black")
                think()
                continue
            if line.startswith(("result", "illegal", "notyourturn", "over")):
                print(line)
                continue
            try:
                mv = chess.Move.from_uci(line)
            except ValueError:
                continue
            if mv not in board.legal_moves:          # the calculator writes castling king->rook: chess960 boards
                print("Ignoring", line)
                continue
            who = "Stockfish" if (board.turn == chess.WHITE) == sf_white else "DM42n"
            if who == "DM42n" or a.manual:
                print(f"{who}: {board.san(mv)}")
            board.push(mv)
            if board.is_game_over(claim_draw=True):
                print("Game over:", board.result(claim_draw=True))
                continue
            think()
    except KeyboardInterrupt:
        pass
    eng.quit()


if __name__ == "__main__":
    sys.exit(main())
