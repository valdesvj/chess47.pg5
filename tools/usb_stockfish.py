#!/usr/bin/env python3
"""
usb_stockfish.py - Stockfish (on Linux) plays Chess 47 on the DM42n over USB, both ways.

On the DM42n: Chess 47, menu page 3 -> MODE until "You vs USB" (it switches USB on). SIDE chooses your colour.
On Linux:     python3 tools/usb_stockfish.py [port] [--elo 2000] [--time 2]

The calculator sends:   CHESS47 hello / newgame <FEN> / opponent white|black / every move (e2e4) / result ...
                        and the screen frames for tools/dm42view.py (skipped here)
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

import chess
import chess.engine


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    t = termios.tcgetattr(fd)
    t[0] = 0; t[1] = 0; t[3] = 0                     # raw: no echo back to the calculator, no line editing
    t[2] |= termios.CLOCAL | termios.CREAD
    termios.tcsetattr(fd, termios.TCSANOW, t)
    return fd


def lines(fd):
    buf = b''
    while True:
        chunk = os.read(fd, 4096)
        if not chunk:
            return
        buf += chunk
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            yield line.strip().decode('ascii', 'replace')


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
        found = sorted(glob.glob('/dev/serial/by-id/*SwissMicros_DM42n*'))
        a.port = found[0] if found else '/dev/ttyACM0'

    eng = chess.engine.SimpleEngine.popen_uci(a.engine)
    if a.elo:
        eng.configure({"UCI_LimitStrength": True, "UCI_Elo": a.elo})
    sf_white = a.side == "white"
    board = chess.Board()
    fd = open_port(a.port)

    def send(s):
        os.write(fd, (s + "\r\n").encode())

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
        for line in lines(fd):
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
