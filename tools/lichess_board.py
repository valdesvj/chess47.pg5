#!/usr/bin/env python3
"""
lichess_board.py - play on Lichess with Chess 47 on the DM42n as your board (the Lichess Board API).

You make your moves on the calculator; they go to Lichess as YOUR moves. Your opponent's moves come back and are
played on the calculator. The calculator's engine and hints are off in this mode: in games as yourself only you
may choose the moves (Lichess fair play). Engines play only on BOT accounts.

Setup (once):
  1. https://lichess.org/account/oauth/token/create  ->  scope "Play games with the board API" (board:play)
  2. echo 'lip_xxxxxxxx' > ~/.config/chess47/lichess.token  (mkdir -p ~/.config/chess47; chmod 600 the file)
  3. pip install --user requests chess   (Arch: sudo pacman -S python-requests python-chess)
On the DM42n: Chess 47 (any mode); the script switches it to "You vs USB" and sets your side.

  python3 tools/lichess_board.py --ai 3              # against Lichess's computer (Stockfish level 1-8)
  python3 tools/lichess_board.py --seek              # a casual game against a person (15+10 by default)
  python3 tools/lichess_board.py --challenge NAME    # challenge a friend
  python3 tools/lichess_board.py                     # wait: play a game you start on the website (or one going on)
  options: --clock 15 --inc 10 --rated --color white|black|random --port /dev/ttyACM1

Board API games must be rapid, classical or correspondence (15+10 is rapid).
"""
import argparse
import glob
import json
import os
import queue
import sys
import termios
import threading
import time

import chess
import requests

API = "https://lichess.org"


# ---------------------------------------------------------------- the calculator
def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    t = termios.tcgetattr(fd)
    t[0] = 0; t[1] = 0; t[3] = 0                     # raw: no echo back, no line editing
    t[2] |= termios.CLOCAL | termios.CREAD
    termios.tcsetattr(fd, termios.TCSANOW, t)
    return fd


def calc_lines(fd, out):
    """the calculator's lines (screen frames skipped) into the queue out"""
    buf, frame = b"", False
    while True:
        chunk = os.read(fd, 4096)
        if not chunk:
            out.put(("calc-gone", None))
            return
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.strip().decode("ascii", "replace")
            if line.endswith("LCD 400 240"):
                frame = True
            elif frame:
                frame = line != "END"
            elif line:
                out.put(("calc", line))


# ---------------------------------------------------------------- Lichess
class Lichess:
    def __init__(self, token):
        self.s = requests.Session()
        self.s.headers["Authorization"] = "Bearer " + token

    def get(self, path):
        r = self.s.get(API + path, timeout=20)
        r.raise_for_status()
        return r.json()

    def post(self, path, data=None):
        r = self.s.post(API + path, data=data or {}, timeout=20)
        if r.status_code >= 400:
            print("Lichess:", r.status_code, r.text.strip()[:200])
        return r

    def stream(self, path, out, tag, method="GET", data=None):
        """an NDJSON stream into the queue out (a thread)"""
        def run():
            try:
                r = self.s.request(method, API + path, data=data, stream=True, timeout=(20, None))
                r.raise_for_status()
                for line in r.iter_lines():
                    if line:
                        out.put((tag, json.loads(line)))
                out.put((tag + "-end", None))
            except Exception as e:                    # noqa: BLE001 - report and end the stream
                out.put((tag + "-error", str(e)))
        threading.Thread(target=run, daemon=True).start()


def clock(ms):
    if ms is None:
        return "-"
    s = int(ms) // 1000
    return f"{s // 60}:{s % 60:02d}"


def calc_to_board(board, text):
    """the calculator's UCI move on python-chess's standard board (Chess 47 writes castling king -> rook)"""
    try:
        mv = chess.Move.from_uci(text[:5])
    except ValueError:
        return None
    if mv in board.legal_moves:
        return mv
    for m in board.legal_moves:                       # e1h1 -> e1g1, e1a1 -> e1c1
        if board.is_castling(m) and m.from_square == mv.from_square:
            rook_side = chess.square_file(mv.to_square) > chess.square_file(mv.from_square)
            king_side = chess.square_file(m.to_square) > chess.square_file(m.from_square)
            if rook_side == king_side:
                return m
    return None


def main():
    ap = argparse.ArgumentParser(description="Chess 47 on the DM42n as a Lichess board")
    ap.add_argument("--port", default=None)
    ap.add_argument("--token", default=os.path.expanduser("~/.config/chess47/lichess.token"))
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--ai", type=int, metavar="LEVEL", help="against Lichess's computer, level 1-8")
    g.add_argument("--seek", action="store_true", help="a game against a person (the lobby)")
    g.add_argument("--challenge", metavar="USER", help="challenge a Lichess user")
    ap.add_argument("--clock", type=int, default=15, help="minutes (default 15)")
    ap.add_argument("--inc", type=int, default=10, help="increment, seconds (default 10)")
    ap.add_argument("--rated", action="store_true")
    ap.add_argument("--color", choices=["white", "black", "random"], default="random")
    a = ap.parse_args()

    token = os.environ.get("LICHESS_TOKEN") or (open(a.token).read().strip() if os.path.exists(a.token) else "")
    if not token:
        sys.exit("no Lichess token: see the setup at the top of this file")
    port = a.port or (sorted(glob.glob("/dev/serial/by-id/*SwissMicros_DM42n*")) or ["/dev/ttyACM0"])[0]
    li = Lichess(token)
    me = li.get("/api/account")["id"]
    print(f"Lichess: {me}   calculator: {port}")
    fd = open_port(port)
    q = queue.Queue()
    threading.Thread(target=calc_lines, args=(fd, q), daemon=True).start()

    def send(s):
        os.write(fd, (s + "\r\n").encode())

    li.stream("/api/stream/event", q, "event")
    clk = {"clock.limit": a.clock * 60, "clock.increment": a.inc, "color": a.color}
    if a.ai:
        r = li.post("/api/challenge/ai", dict(clk, level=a.ai))
        print("Against Lichess AI level", a.ai, "" if r.ok else "(refused)")
    elif a.seek:
        li.stream("/api/board/seek", q, "seek", "POST",
                  {"rated": str(a.rated).lower(), "time": a.clock, "increment": a.inc, "color": a.color})
        print("Seeking a game ...")
    elif a.challenge:
        r = li.post("/api/challenge/" + a.challenge, dict(clk, rated=str(a.rated).lower()))
        print("Challenge sent to", a.challenge, "" if r.ok else "(refused)")
    else:
        print("Waiting for a game (start one on lichess.org) ...")

    game = None                       # its id
    me_white = True
    board = chess.Board()
    sent = set()                      # my moves sent to Lichess, not yet confirmed

    def sync(moves, status, wtime, btime):
        nonlocal board
        known = len(board.move_stack)
        for i, u in enumerate(moves):
            if i < known:
                continue
            mv = chess.Move.from_uci(u)
            mine = board.turn == (chess.WHITE if me_white else chess.BLACK)
            if not mine:
                send(board.uci(mv, chess960=False))       # the opponent's move: to the calculator
            sent.discard(u)
            board.push(mv)
        if status not in ("created", "started"):
            send(f"msg {status.upper()}  {board.result(claim_draw=True)}")
            print("Game over:", status)
        elif wtime is not None:
            send(f"msg W {clock(wtime)}  B {clock(btime)}")

    while True:
        tag, v = q.get()
        if tag == "calc":
            if v.startswith(("illegal", "notyourturn", "over")):
                print("calculator:", v)
                continue
            if game is None:
                continue
            mv = calc_to_board(board, v)
            if mv is None:
                continue                                    # not a move (hello, newgame, opponent ...)
            if board.turn != (chess.WHITE if me_white else chess.BLACK):
                continue                                    # the echo of the opponent's move
            u = board.uci(mv, chess960=False)
            if u in sent:
                continue
            sent.add(u)
            print("me:", board.san(mv))
            if not li.post(f"/api/board/game/{game}/move/{u}").ok:
                sent.discard(u)
                send("msg Lichess refused the move")
        elif tag == "event" and v.get("type") == "gameStart" and game is None:
            game = v["game"].get("gameId") or v["game"].get("id")
            print("Game:", f"{API}/{game}")
            li.stream(f"/api/board/game/stream/{game}", q, "game")
        elif tag == "game" and v.get("type") == "gameFull":
            me_white = v.get("white", {}).get("id") == me
            opp = v["black" if me_white else "white"]
            name = opp.get("name") or ("AI level %s" % opp.get("aiLevel", "?"))
            fen = v.get("initialFen", "startpos")
            board = chess.Board() if fen == "startpos" else chess.Board(fen)
            send(f"newgame {'white' if me_white else 'black'}" + ("" if fen == "startpos" else " " + fen))
            time.sleep(0.3)
            st = v["state"]
            moves = st["moves"].split() if st["moves"] else []
            for u in moves:                                  # catch the calculator up
                send("play " + u)
                board.push_uci(u)
            print(f"You play {'white' if me_white else 'black'} against {name} ({opp.get('rating', '-')})")
            send(f"msg vs {name} {opp.get('rating', '')}".strip())
            sync(moves, st["status"], st.get("wtime"), st.get("btime"))
        elif tag == "game" and v.get("type") == "gameState":
            sync(v["moves"].split() if v["moves"] else [], v["status"], v.get("wtime"), v.get("btime"))
        elif tag == "game-end":
            print("The game is over. Ctrl+C to quit, or start another game on lichess.org")
            game = None
        elif tag.endswith("-error"):
            print(tag, v)
        elif tag == "calc-gone":
            sys.exit("the calculator's port closed")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
