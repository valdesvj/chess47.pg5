#!/usr/bin/env python3
"""Puzzles 47 (SHIFT 4) in the PC simulator (build/sim): the puzzle shown, a mate found, a wrong move taken back,
another mate than the solution's accepted (the mate solver), a move that does not mate in time refused, SOLVE, EXIT
back to the game put aside, and the power off in puzzles keeping that game (AUTO.TXT) and the progress (PUZZLES.TXT).
The moves are read back from the panel and replayed with python-chess.

    make sim && python3 test/test_puzzles.py"""
import os, re, subprocess, sys, tempfile

import chess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, 'build', 'sim')
bad = 0

H = open(os.path.join(ROOT, 'src', 'puzzles.h'), encoding='utf-8').read()
PUZ = re.findall(r'\{\s*"([^"]+)",\s*PZ_(\w+),\s*(\d+),\s*"([^"]*)",\s*"([^"]*)"\s*\}', H)


def run(keys, d=None):
    d = d or tempfile.mkdtemp()
    subprocess.run([SIM] + keys, cwd=d, capture_output=True, timeout=300)
    out = {'dir': d}
    for k in keys:
        if k.startswith('SNAP:'):
            n = k[5:]
            rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(d, n + '.txt')) if l.strip()]
            out[n] = [(int(r[2]), int(r[1]), r[4]) for r in rows]
    return out


def panel(snap):
    """the panel's texts, top down (left of the board)"""
    return [s for y, x, s in sorted(snap) if x < 190 and y < 205]


def ok(cond, what, got=''):
    global bad
    bad += not cond
    print('  %s  %s%s' % ('ok  ' if cond else 'FAIL', what, '' if cond else '   got: %r' % (got,)))


class Cursor:
    """the cursor's key presses (the screen's directions: flipped when Black plays)"""
    KEYS = {(1, 1): '9', (-1, 1): '7', (1, -1): '3', (-1, -1): '1', (1, 0): '6', (-1, 0): '4', (0, 1): '8', (0, -1): '2'}

    def __init__(self, flip):
        self.flip = flip
        self.sq = chess.E7 if flip else chess.E2

    def go(self, sq):
        dx = chess.square_file(sq) - chess.square_file(self.sq)
        dy = chess.square_rank(sq) - chess.square_rank(self.sq)
        if self.flip:
            dx, dy = -dx, -dy
        keys = []
        while dx or dy:
            sx, sy = (dx > 0) - (dx < 0), (dy > 0) - (dy < 0)
            keys.append(self.KEYS[(sx, sy)])
            dx -= sx; dy -= sy
        self.sq = sq
        return keys

    def move(self, uci):
        m = chess.Move.from_uci(uci)
        return self.go(m.from_square) + ['5'] + self.go(m.to_square) + ['5']


def board_of(i, snap):
    """the puzzle's position with the moves the panel lists (SAN) played; the lines scrolled off the panel's top
    are the solution's first moves (the tests play them)"""
    b = chess.Board(PUZ[i][0])
    sans, first = [], None
    for t in panel(snap):
        if re.match(r'^\d+\.$', t):
            first = first or int(t[:-1])
            continue
        if t == '...':
            continue
        if re.match(r'^[KQRBNO]?[a-h1-8x=+#O-]+[QRBN]?[+#]?$', t) and re.search(r'[a-h][1-8]|O-O', t):
            sans.append(t)
    hidden = 0 if first is None else 2 * (first - b.fullmove_number) - (b.turn == chess.BLACK and first > b.fullmove_number)
    for u in PUZ[i][4].split()[:max(0, hidden)]:
        b.push_uci(u)
    for s in sans:
        b.push_san(s)
    return b


def nav(i):
    """from puzzle 1 to puzzle i+1: NEXT"""
    return ['F2'] * i


def main():
    if not os.path.exists(SIM):
        sys.exit('make sim first')

    print('== SHIFT 4: puzzle 1 shown')
    r = run(['SHIFT', '4', 'SNAP:a'])
    p = panel(r['a'])
    ok(p[:2] == ['PUZZLES 47', '1/47'], 'the header: %s' % p[:2], p)
    ok("Fool's mate" in p and 'Black to play and' in p and 'mate in 1' in p, 'the title and the stipulation', p)
    keys = [t for y, x, t in r['a'] if y > 205]
    ok(keys == ['PREV', 'NEXT', 'HINT', 'SOLVE', 'RETRY', 'EXIT'], 'the soft keys: %s' % keys, keys)

    print('== puzzle 1 solved (Qh4#), puzzle 2: a wrong move taken back')
    c = Cursor(True)
    k = ['SHIFT', '4'] + c.move('d8h4') + ['PAUSE:300', 'SNAP:a', 'F2', 'SNAP:b']
    c = Cursor(False)
    k += c.move('h5h7') + ['PAUSE:500', 'SNAP:c']
    r = run(k)
    ok('Solved! 1 of 47' in panel(r['a']), 'Solved! 1 of 47', panel(r['a']))
    ok('* 1/47' in panel(r['a']), 'the header marks it solved', panel(r['a']))
    ok('2/47' in panel(r['b']) and "Scholar's mate" in panel(r['b']), 'NEXT: puzzle 2', panel(r['b']))
    ok('No mate: try again' in panel(r['c']) and 'Qxh7+' not in panel(r['c']), 'Qxh7+ refused and taken back',
       panel(r['c']))

    print('== puzzle 23 (Loyd, Charles XII): the key Rxg3, the defence Bxg3; 2.hxg3 stalemates: refused')
    c = Cursor(False)
    k = ['SHIFT', '4'] + nav(22) + ['SNAP:a'] + c.move('g7g3') + ['PAUSE:1500', 'SNAP:b']
    k += c.move('h2g3') + ['PAUSE:3000', 'SNAP:c']
    r = run(k)
    ok('23/47' in panel(r['a']) and 'mate in 3' in panel(r['a']), 'puzzle 23, mate in 3', panel(r['a']))
    b = board_of(22, r['b'])
    ok(len(b.move_stack) == 2 and b.move_stack[1].uci() == 'f2g3', 'the key Rxg3 and the defence Bxg3', b.move_stack)
    b = board_of(22, r['c'])
    ok('Not the solution' in panel(r['c']) and len(b.move_stack) == 2, '2.hxg3 refused, taken back', panel(r['c']))

    print('== puzzle 36 (Lasker - Thomas): 14.f4+ is not the line\'s 14.h4+, but mates in time too (the solver)')
    c = Cursor(False)
    k = ['SHIFT', '4'] + nav(35)
    for u in ('h5h7', 'e4f6', 'e5g4'):
        k += c.move(u) + ['PAUSE:600']
    k += c.move('f2f4') + ['PAUSE:9000', 'SNAP:a']
    r = run(k)
    b = board_of(35, r['a'])
    ok(len(b.move_stack) == 8 and b.move_stack[6].uci() == 'f2f4' and 'Your move' in panel(r['a']),
       '14.f4+ accepted, the defence played: %s' % ' '.join(m.uci() for m in b.move_stack), panel(r['a']))

    print('== puzzle 23: 2.Kf6 is no mate in 2 (the solver refuses it)')
    c = Cursor(False)
    k = ['SHIFT', '4'] + nav(22) + c.move('g7g3') + ['PAUSE:1500'] + c.move('f5f6') + ['PAUSE:3000', 'SNAP:a']
    r = run(k)
    b = board_of(22, r['a'])
    ok('Not the solution' in panel(r['a']) and len(b.move_stack) == 2, 'refused and taken back', panel(r['a']))

    print('== SOLVE, then EXIT: the game put aside comes back (1.e4 played before)')
    k = ['5', '8', '8', '5', 'PAUSE:1500', 'SNAP:g', 'SHIFT', '4', 'F2', 'F2', 'F2', 'F2', 'F2', 'F4', 'PAUSE:200',
         'SNAP:a', 'F6', 'SNAP:b']
    r = run(k)
    g = panel(r['g'])
    p = panel(r['a'])
    ok('The solution' in p and 'Bxf7+' in p and 'Nd5#' in p, 'puzzle 6 (Legall) shown: %s' % p, p)
    b = panel(r['b'])
    ok('Game resumed' in b and 'e4' in b and any(t.startswith('L4') for t in b), 'the game is back: %s' % b, b)
    ok([t for t in g if re.match(r'^[a-h1-8NBRQKx]+$', t) and t != 'e4'] ==
       [t for t in b if re.match(r'^[a-h1-8NBRQKx]+$', t) and t != 'e4'], 'with the CPU\'s reply too', (g, b))

    print('== the power off in puzzles: AUTO.TXT the game put aside, PUZZLES.TXT the progress')
    c = Cursor(True)
    r = run(['5', '8', '8', '5', 'PAUSE:1500', 'SHIFT', '4'] + c.move('d8h4') + ['PAUSE:300', 'F2', 'SHIFT', 'EXIT'])
    d = r['dir']
    auto = open(os.path.join(d, 'simdisk', 'CHESS47', 'AUTO.TXT')).read() if os.path.exists(os.path.join(d, 'simdisk', 'CHESS47', 'AUTO.TXT')) else ''
    pzf = open(os.path.join(d, 'simdisk', 'CHESS47', 'PUZZLES.TXT')).read() if os.path.exists(os.path.join(d, 'simdisk', 'CHESS47', 'PUZZLES.TXT')) else ''
    ok(auto.startswith('CHESS47') and 'e2e4' in auto and 'PPPPPPPP/RNBQKBNR' in auto, 'AUTO.TXT: the game, 1.e4', auto)
    ok(pzf.split('\n')[:2] == ['PUZZLES47 1', '2'] and pzf.split('\n')[2].startswith('1000'),
       'PUZZLES.TXT: puzzle 2 shown, puzzle 1 solved', pzf)
    r = run(['SHIFT', '4', 'SNAP:a'], d)
    ok('* 1/47' not in panel(r['a']) and '2/47' in panel(r['a']) and 'Solved 1 of 47' in panel(r['a']),
       'SHIFT 4 again: puzzle 2, 1 solved', panel(r['a']))

    print('all good' if not bad else '%d FAILED' % bad)
    return 1 if bad else 0


sys.exit(main())
