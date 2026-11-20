#!/usr/bin/env python3
"""The clock in the PC simulator (build/sim): the tournament controls, CLOCK during a game (only OFF, pressed twice),
the time used counting up when OFF, SAVE / LOAD, and the same in Xiangqi.

    make sim && python3 test/test_clock.py

Each case runs the real main.c with scripted keys (WAIT:ms moves the simulator's clock on) and reads the screen's
text: the clock line (y 1) and the status line (y 42)."""
import os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, 'build', 'sim')
bad = 0

TWO = ['F6', 'F6', 'F2', 'F6']          # MODE (page 3): 2 players, back to page 1
CLOCK = ['F6', 'F2', 'F6', 'F6']        # page 2 CLOCK, back to page 1
E4 = ['5', '8', '8', '5']               # e2-e4 (the cursor starts on e2)
E5 = ['8', '8', '8', '5', '2', '2', '5']  # from e4: e7-e5
D4 = ['2', '2', '2', '4', '5', '8', '8', '5']  # from e5: d2-d4


def run(keys, d=None):
    """the keys -> {snap: (the clock texts, the status)}; d: the simulator's folder (kept between runs: the disk)"""
    d = d or tempfile.mkdtemp()
    snaps = [k[5:] for k in keys if k.startswith('SNAP:')]
    subprocess.run([SIM] + keys, cwd=d, capture_output=True, timeout=120)
    out = {}
    for s in snaps:
        rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(d, s + '.txt')) if l.strip()]
        clock = [r[4].strip() for r in sorted(rows, key=lambda r: int(r[1])) if r[2] == '1' and int(r[1]) < 200]
        status = [r[4] for r in rows if r[2] == '42' and int(r[1]) < 200]
        out[s] = (clock, status[-1] if status else '')
    return out, d


def ok(cond, what, got):
    global bad
    bad += not cond
    print('  %s  %s%s' % ('ok  ' if cond else 'FAIL', what, '' if cond else '   got: %r' % (got,)))


def main():
    if not os.path.exists(SIM):
        sys.exit('make sim first')

    print('== the time controls before the first move (CLOCK cycles them)')
    keys = TWO + ['F6']
    for i in range(8):
        keys += ['F2', 'SNAP:c%d' % i]
    r, _ = run(keys)
    names = [r['c%d' % i][1] for i in range(8)]
    want = ['Clock 1+0', 'Clock 3+2', 'Clock 5+3', 'Clock 10+5', 'Clock 15+10', 'Clock 25+10', 'Clock 90+30', 'Clock OFF']
    ok(names == want, 'OFF 1+0 3+2 5+3 10+5 15+10 25+10 90+30, then OFF again', names)
    ok(r['c0'][0] == ['W 1:00', 'B 1:00'], '1+0 shows 1:00 each', r['c0'][0])
    ok(r['c6'][0] == ['W 1:30:00', 'B 1:30:00'], '90+30 fits half the panel (W 1:30:00)', r['c6'][0])
    ok(r['c7'][0][0] == 'CHESS 47', 'OFF: the title before the first move', r['c7'][0])

    print('== 10+5: the increment, the first move free')
    r, _ = run(TWO + CLOCK * 4 + E4 + ['WAIT:20000'] + E5 + ['SNAP:a', 'WAIT:30000'] + D4 + ['SNAP:b'])
    ok(r['a'][0] == ['W 10:00', 'B 9:45'], 'after 1. e4 (free) e5 (20 s): W 10:00, B 9:45 (600 - 20 + 5)', r['a'][0])
    ok(r['b'][0] == ['W 9:35', 'B 9:45'], 'after 2. d4 (30 s): W 9:35 (600 - 30 + 5)', r['b'][0])

    print('== CLOCK during the game: a question, then OFF with the time used')
    base = TWO + CLOCK * 4 + E4 + ['WAIT:20000'] + E5 + ['WAIT:30000'] + D4 + ['WAIT:7000']
    r, _ = run(base + ['F6', 'F2', 'SNAP:ask', '4', 'SNAP:cancel', 'F2', 'F2', 'SNAP:off', 'WAIT:60000', 'SNAP:later',
                       'F2', 'SNAP:again'])
    ok(r['ask'][1] == 'Stop clock? CLOCK', 'the first press asks', r['ask'][1])
    ok(r['ask'][0] == ['W 9:35', 'B 9:38'], 'and the clock still runs (B 9:45 - 7 s)', r['ask'][0])
    ok(r['cancel'][1] != 'Clock stopped' and r['cancel'][0][0] == 'W 9:35', 'another key cancels',
       r['cancel'])
    ok(r['off'][1] == 'Clock stopped', 'the second press stops it', r['off'][1])
    ok(r['off'][0] == ['W 0:30', 'B 0:27'], 'each side\'s time used: W 30 s, B 20 + 7 s', r['off'][0])
    ok(r['later'][0] == ['W 0:30', 'B 1:27'], 'then it counts up (B + 60 s)', r['later'][0])
    ok(r['again'][1] == 'Clock: set at NEW', 'a stopped clock is not set again in the game', r['again'][1])

    print('== OFF from the start: the time used, nobody loses on time')
    r, _ = run(TWO + E4 + ['WAIT:3600000'] + E5 + ['WAIT:5000', 'SNAP:a'])
    ok(r['a'][0] == ['W 0:05', 'B 1:00:00'], 'an hour for e5: B 1:00:00, W counting 0:05', r['a'][0])
    ok(r['a'][1] in ('White to move', ''), 'the game goes on', r['a'][1])

    print('== the time runs out (1+0)')
    r, _ = run(TWO + CLOCK + E4 + ['WAIT:61000'] + E5 + ['SNAP:a', 'F6', 'F2', 'SNAP:b'])
    ok('0.0' in r['a'][0][1] and 'flag' in r['a'][1], 'Black at 0.0, the game lost on time', r['a'])
    ok(r['b'][0] == r['a'][0], 'the game over: CLOCK leaves the clocks (the control is chosen at NEW)', r['b'][0])
    r, _ = run(TWO + CLOCK + E4 + ['WAIT:61000'] + E5 + ['F1', 'F6', 'F2', 'SNAP:c'])
    ok(r['c'][1] == 'Clock 3+2' and r['c'][0] == ['W 3:00', 'B 3:00'], 'NEW, then CLOCK cycles again', r['c'])

    print('== SAVE / LOAD keep a stopped clock as OFF with the time used')
    r, d = run(base + ['F6', 'F2', 'F2', 'F4', 'F6', 'F6'])
    r, _ = run(TWO + ['F6', 'F5', 'SNAP:a'], d)
    ok(r['a'][1] == 'Loaded' and r['a'][0] == ['W 0:30', 'B 0:27'], 'loaded: W 0:30, B 0:27', r['a'])

    print('== Xiangqi: the same rules (R and B)')
    X = ['SHIFT', 'F1'] + TWO
    C2 = ['5', '4', '4', '4', '5']        # the cannon h3-e3 (C2=5)
    r, _ = run(X + CLOCK * 4 + ['SNAP:a'] + C2 + ['WAIT:20000', 'SNAP:b', 'F6', 'F2', 'SNAP:ask', 'F2', 'SNAP:off',
                                                  'WAIT:10000', 'SNAP:later'])
    ok(r['a'][0] == ['R 10:00', 'B 10:00'], 'Xiangqi 10+5', r['a'][0])
    ok(r['b'][0] == ['R 10:00', 'B 9:40'], 'Red\'s first move free, Black\'s 20 s', r['b'][0])
    ok(r['ask'][1] == 'Stop clock? CLOCK' and r['off'][1] == 'Clock stopped', 'the question, then OFF',
       (r['ask'][1], r['off'][1]))
    ok(r['later'][0] == ['R 0:00', 'B 0:30'], 'the time used counts up: B 30 s', r['later'][0])
    r, _ = run(X + ['SNAP:a'] + C2 + ['WAIT:9000', 'SNAP:b'])
    ok(r['a'][0][0] == 'XIANGQI 47' and r['b'][0] == ['R 0:00', 'B 0:09'], 'OFF: the title, then the time used',
       (r['a'][0], r['b'][0]))

    print('== SHIFT F1: each game keeps its own clock (no time given back)')
    chess = TWO + CLOCK * 4 + E4 + ['WAIT:20000'] + E5 + ['WAIT:30000'] + D4 + ['WAIT:7000']   # W 9:35, B 9:38
    r, _ = run(chess + ['SNAP:a', 'SHIFT', 'F1', 'SNAP:x', 'F6', 'F2', 'F2', 'F6', 'F6', 'SNAP:x2'] + C2 +
               ['WAIT:40000', 'SNAP:x3', 'SHIFT', 'F1', 'SNAP:b', 'WAIT:5000', 'SNAP:c', 'SHIFT', 'F1', 'SNAP:x4'])
    ok(r['a'][0] == ['W 9:35', 'B 9:38'], 'chess 10+5 before the switch', r['a'][0])
    ok(r['x'][0] == ['R 10:00', 'B 10:00'], 'Xiangqi the first time: the clock as set (10+5)', r['x'][0])
    ok(r['x2'][0] == ['R 25:00', 'B 25:00'], 'Xiangqi before its first move: CLOCK twice -> 25+10', r['x2'][0])
    ok(r['x3'][0] == ['R 25:00', 'B 24:20'], 'Xiangqi 25+10 running (B 40 s)', r['x3'][0])
    ok(r['b'][0] == ['W 9:35', 'B 9:38'], 'back to chess: its 10+5 times as they were (not 10:00)', r['b'][0])
    ok(r['c'][0] == ['W 9:35', 'B 9:33'], 'and its clock runs on (B - 5 s)', r['c'][0])
    ok(r['x4'][0] == ['R 25:00', 'B 24:20'], 'back to Xiangqi: 25+10 kept too', r['x4'][0])
    r, _ = run(chess + ['F6', 'F2', 'F2', 'F6', 'F6', 'WAIT:10000', 'SHIFT', 'F1', 'SHIFT', 'F1', 'SNAP:a', 'F6', 'F2', 'SNAP:b'])
    ok(r['a'][0] == ['W 0:30', 'B 0:37'], 'a stopped clock stays stopped (time used) after the switches', r['a'][0])
    ok(r['b'][1] == 'Clock: set at NEW', 'and is not set again in the game', r['b'][1])

    print('%d failed' % bad if bad else 'all ok')
    sys.exit(1 if bad else 0)


main()
