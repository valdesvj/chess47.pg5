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

During the game, on the calculator's menu page 1 (Lichess's own rules):
  F4  RESIGN (press twice); ABORT (twice) before each side has moved, as Lichess allows
  F5  DRAW offers a draw (DRAW* while it waits; a move by the opponent declines it); when the opponent offers,
      ACCEPT (twice) agrees, BSP declines, and your next move declines it too; CLAIM when the opponent has left
      and Lichess lets you claim the win
  SHIFT BSP resigns at once

As a BOT (--bot): the calculator's own engine plays ("CPU vs USB"), on a Lichess BOT account (a new account that has
never played, upgraded once: curl -d '' https://lichess.org/api/bot/account/upgrade -H "Authorization: Bearer TOKEN";
its token needs the scope bot:play). Keep that token apart: --token ~/.config/chess47/bot.token
  python3 tools/lichess_board.py --bot --token ~/.config/chess47/bot.token            # accept challenges
  python3 tools/lichess_board.py --bot --level 9 --challenge NAME                     # challenge someone
  options: --level 0-9 (default 9), --speeds blitz,rapid,classical,correspondence
The script sends the game's clock to the calculator before each of its moves; it thinks about 1/30 of its time.
On the DM42n: Chess 47, USB on (page 3, F4); the script switches it to "CPU vs USB".
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm42port  # noqa: E402

import chess
import requests

API = "https://lichess.org"


# ---------------------------------------------------------------- the calculator
def calc_lines(port, out):
    """the calculator's lines (the screen and DMCP's own lines skipped: tools/dm42port.py) into the queue out"""
    for line in port.lines():
        out.put(("calc", line))
    out.put(("calc-gone", None))


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
    ap.add_argument("--bot", action="store_true", help="the calculator's engine plays (a Lichess BOT account)")
    ap.add_argument("--level", type=int, default=9, choices=range(10), help="--bot: the engine's level (default 9)")
    ap.add_argument("--speeds", default="blitz,rapid,classical,correspondence",
                    help="--bot: the challenges accepted (default blitz,rapid,classical,correspondence)")
    a = ap.parse_args()
    if a.bot and a.seek:
        sys.exit("--seek: BOT accounts can't use the lobby; wait for challenges or use --challenge")
    api = "/api/bot" if a.bot else "/api/board"
    speeds = set(a.speeds.split(","))

    token = os.environ.get("LICHESS_TOKEN") or (open(a.token).read().strip() if os.path.exists(a.token) else "")
    if not token:
        sys.exit("no Lichess token: see the setup at the top of this file")
    port = a.port or dm42port.find()
    li = Lichess(token)
    acct = li.get("/api/account")
    me = acct["id"]
    if a.bot and acct.get("title") != "BOT":
        sys.exit(f"{me} is not a BOT account: see --bot at the top of this file")
    print(f"Lichess: {me}{' (BOT, level %d)' % a.level if a.bot else ''}   calculator: {port}")
    calc = dm42port.Port(port)          # DMCP's serial console (channel 2) or the TinyUSB port
    q = queue.Queue()
    threading.Thread(target=calc_lines, args=(calc, q), daemon=True).start()

    def send(s):
        calc.send(s)

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
    elif a.bot:
        print("Waiting for challenges ...")
    else:
        print("Waiting for a game (start one on lichess.org) ...")

    game = None                       # its id
    me_white = True
    board = chess.Board()
    sent = set()                      # my moves sent to Lichess, not yet confirmed
    inc = 0                           # the game's increment, ms
    offers = {"mine": False, "theirs": False}     # draw offers pending (gameState wdraw / bdraw)
    ACTIONS = {"resign": "resign", "abort": "abort", "draw offer": "draw/yes", "draw accept": "draw/yes",
               "draw decline": "draw/no", "claim": "claim-victory"}

    def draws(st):
        """the draw offers in a game state: the calculator told when the opponent's offer comes or goes, and when
        ours is declined (it disappears while the game goes on)"""
        mine = bool(st.get("wdraw" if me_white else "bdraw"))
        theirs = bool(st.get("bdraw" if me_white else "wdraw"))
        if theirs != offers["theirs"]:
            send("drawoffer" if theirs else "drawoffer off")
            print("Draw offered by the opponent" if theirs else "The opponent's draw offer is gone")
        if offers["mine"] and not mine and st.get("status") in ("created", "started"):
            send("drawdeclined")
            print("Our draw offer was declined")
        offers["mine"], offers["theirs"] = mine, theirs
    ENDS = {"mate": "Checkmate", "resign": "Resignation", "timeout": "Left the game", "outoftime": "Time out",
            "draw": "Draw", "stalemate": "Stalemate", "aborted": "Aborted", "noStart": "No start"}

    def sync(moves, status, wtime, btime, winner=None):
        nonlocal board
        known = len(board.move_stack)
        for i, u in enumerate(moves):
            if i < known:
                continue
            mv = chess.Move.from_uci(u)
            mine = board.turn == (chess.WHITE if me_white else chess.BLACK)
            if not mine:
                if a.bot and wtime is not None:           # the engine's time left, before its turn
                    send(f"clock {int(btime if me_white else wtime)} {int(inc)}")
                send(board.uci(mv, chess960=False))       # the opponent's move: to the calculator
            sent.discard(u)
            board.push(mv)
        if status not in ("created", "started"):
            res = ({"white": "1-0", "black": "0-1"}.get(winner) or ("1/2" if status in ("draw", "stalemate") else ""))
            send(f"end {ENDS.get(status, status)} {res}".strip())
            print("Game over:", status)
        elif wtime is not None and not a.bot:
            send(f"msg W {clock(wtime)}  B {clock(btime)}")

    while True:
        tag, v = q.get()
        if tag == "calc":
            if v.startswith(("illegal", "notyourturn", "over")):
                print("calculator:", v)
                continue
            if game is None:
                continue
            if v in ACTIONS:                                # resign, abort, draw offer / accept / decline, claim
                r = li.post(f"{api}/game/{game}/{ACTIONS[v]}")
                print("me:", v, "" if r.ok else "(Lichess refused: %s)" % r.text.strip()[:80])
                if v == "draw offer":
                    offers["mine"] = r.ok
                if not r.ok:
                    send(f"msg Lichess refused: {v}")
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
            if not li.post(f"{api}/game/{game}/move/{u}").ok:
                sent.discard(u)
                send("msg Lichess refused the move")
        elif tag == "event" and v.get("type") == "challenge" and a.bot:
            c = v["challenge"]
            if c["challenger"]["id"] == me:
                continue                                    # our own challenge
            why = ("later" if game is not None else "standard" if c["variant"]["key"] != "standard"
                   else "timeControl" if c["speed"] not in speeds else None)
            who = f"{c['challenger']['name']} ({c['speed']}, {c['variant']['key']})"
            if why:
                li.post(f"/api/challenge/{c['id']}/decline", {"reason": why})
                print("Declined", who, why)
            else:
                li.post(f"/api/challenge/{c['id']}/accept")
                print("Accepted", who)
        elif tag == "event" and v.get("type") == "gameStart" and game is None:
            game = v["game"].get("gameId") or v["game"].get("id")
            print("Game:", f"{API}/{game}")
            li.stream(f"{api}/game/stream/{game}", q, "game")
        elif tag == "game" and v.get("type") == "gameFull":
            me_white = v.get("white", {}).get("id") == me
            opp = v["black" if me_white else "white"]
            name = opp.get("name") or ("AI level %s" % opp.get("aiLevel", "?"))
            fen = v.get("initialFen", "startpos")
            board = chess.Board() if fen == "startpos" else chess.Board(fen)
            inc = (v.get("clock") or {}).get("increment", 0)
            if a.bot:
                send(f"level {a.level}")
            send(f"{'botgame' if a.bot else 'newgame'} {'white' if me_white else 'black'}"
                 + ("" if fen == "startpos" else " " + fen))
            time.sleep(0.3)
            st = v["state"]
            moves = st["moves"].split() if st["moves"] else []
            for u in moves:                                  # catch the calculator up
                send("play " + u)
                board.push_uci(u)
            side = 'white' if me_white else 'black'
            print(f"{'Chess 47 plays' if a.bot else 'You play'} {side} against {name} ({opp.get('rating', '-')})")
            send(f"msg vs {name} {opp.get('rating', '')}".strip())
            if a.bot:
                if st.get("wtime") is not None:
                    send(f"clock {int(st['wtime'] if me_white else st['btime'])} {int(inc)}")
                send("go")                                   # the engine may move now
                li.post(f"/api/bot/game/{game}/chat", {"room": "player", "text":
                        f"Chess 47 (level {a.level}) thinking on a SwissMicros DM42n calculator. Good luck!"})
            sync(moves, st["status"], st.get("wtime"), st.get("btime"), st.get("winner"))
            offers.update(mine=False, theirs=False)
            draws(st)
        elif tag == "game" and v.get("type") == "gameState":
            sync(v["moves"].split() if v["moves"] else [], v["status"], v.get("wtime"), v.get("btime"), v.get("winner"))
            draws(v)
        elif tag == "game" and v.get("type") == "opponentGone":
            if not v.get("gone"):
                send("claim off")
                send("msg The opponent is back")
                print("The opponent is back")
            else:
                secs = v.get("claimWinInSeconds")
                if secs:
                    send(f"msg Opponent left: claim in {secs} s")
                    threading.Timer(secs, q.put, args=(("claim-ready", game),)).start()
                else:
                    send("claim on")
                print("The opponent left the game", f"(claim in {secs} s)" if secs else "(claim now)")
        elif tag == "claim-ready" and v == game:
            send("claim on")
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
