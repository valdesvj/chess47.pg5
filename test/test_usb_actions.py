#!/usr/bin/env python3
"""test_usb_actions.py - vs USB (Lichess, Stockfish): resign, abort, draw offers and claiming the win, in the PC
simulator (build/sim, make sim; either USB build): the PC's lines from acm.in, the keys scripted, Chess 47's lines
read back from acm.out (tools/dm42port.py) and the menu's labels from the snapshot's text list.
    make sim && python3 test/test_usb_actions.py"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import dm42port  # noqa: E402

SIM = os.path.join(ROOT, 'build', 'sim')
START = ['F6', 'F6', 'F4'] + ['WAIT:300'] * 8            # USB on (menu page 3); the game starts on page 1
OPENING = ['newgame white', 'play e2e4', 'play e7e5', 'play g1f3', 'play b8c6']
bad = 0


def run(pc, keys, start=None):
    """the PC's lines, the keys -> (Chess 47's lines after its hello / newgame / opponent, the menu labels at the end)"""
    d = tempfile.mkdtemp()
    open(os.path.join(d, 'acm.in'), 'w').write('\n'.join(pc) + '\n')
    subprocess.run([SIM] + (start or START) + keys + ['SNAP:end'], cwd=d, env=dict(os.environ, ACM_FREE='1'),
                   capture_output=True, timeout=120)
    lines = list(dm42port.Port(os.path.join(d, 'acm.out')).lines())
    labels = [f[4] for f in (l.rstrip('\n').split('\t') for l in open(os.path.join(d, 'end.txt'))) if f[3] == '2']
    moves = lambda l: len(l) in (4, 5) and l[0] in 'abcdefgh' and l[1] in '12345678'
    return [l for l in lines if not l.startswith(('CHESS47', 'newgame', 'opponent', 'result')) and not moves(l)], labels


def ok(cond, what, got):
    global bad
    bad += not cond
    print('  %s  %s%s' % ('ok  ' if cond else 'FAIL', what, '' if cond else '   got: %r' % (got,)))


def main():
    if not os.path.exists(SIM):
        sys.exit('make sim first')
    print('== vs USB: resign, abort, draws, claim (build/sim)')
    out, lab = run(OPENING, ['F4', 'F4'])
    ok(out == ['resign'] and 'RESIGN' in lab, 'RESIGN twice -> resign', (out, lab))
    out, _ = run(OPENING, ['F4', 'F3', 'F4'])
    ok(out == [], 'RESIGN, another key, RESIGN -> nothing sent (the first press is cancelled)', out)
    out, _ = run(OPENING, ['SHIFT', 'BSP'])
    ok(out == ['resign'], 'SHIFT BSP -> resign', out)
    out, lab = run(['newgame white', 'play e2e4'], ['F4', 'F4'])
    ok(out == ['abort'] and 'ABORT' in lab, 'one move played: ABORT twice -> abort', (out, lab))
    out, lab = run(OPENING, ['F5'])
    ok(out == ['draw offer'] and 'DRAW*' in lab, 'DRAW -> draw offer, the key shows DRAW*', (out, lab))
    out, lab = run(OPENING + ['drawoffer'], [])
    ok(out == [] and 'ACCEPT' in lab, 'the PC: drawoffer -> the key shows ACCEPT', (out, lab))
    out, _ = run(OPENING + ['drawoffer'], ['F5', 'F5'])
    ok(out == ['draw accept'], 'ACCEPT twice -> draw accept', out)
    out, _ = run(OPENING + ['drawoffer'], ['F5'])
    ok(out == [], 'ACCEPT once -> nothing sent yet', out)
    out, lab = run(OPENING + ['drawoffer'], ['BSP'])
    ok(out == ['draw decline'] and 'DRAW' in lab, 'BSP -> draw decline, the key DRAW again', (out, lab))
    out, lab = run(OPENING + ['drawoffer', 'drawoffer off'], [])
    ok(out == [] and 'ACCEPT' not in lab, 'the PC: drawoffer off -> no ACCEPT', (out, lab))
    out, lab = run(OPENING + ['claim on'], ['F5'])
    ok(out == ['claim'] and 'CLAIM' in lab, 'the PC: claim on -> CLAIM -> claim', (out, lab))
    out, _ = run(OPENING + ['end Resignation 1-0'], ['F4', 'F4', 'F5'])
    ok(out == [], 'the game over: F4 / F5 send nothing', out)
    out, lab = run(OPENING, ['F6', 'F6', 'F4', 'F4'], start=['F6', 'F6', 'F4'] + ['WAIT:300'] * 8)
    ok(out == [] and 'RESIGN' not in lab, 'on menu page 3 during the game: F4 (USB) keeps USB on, sends nothing', (out, lab))
    out, lab = run(OPENING, ['F4', 'F4'], start=['F6', 'F6', 'F4'] + ['WAIT:300'] * 8)
    ok(out == ['resign'] and 'RESIGN' in lab, 'USB switched on from page 3: the game starts on page 1, F4 F4 resigns',
       (out, lab))
    out, lab = run(['newgame white', 'play e2e4', 'play e7e5'], [])
    ok('UNDO' not in lab and 'HINT' not in lab, 'vs USB the menu has no UNDO / HINT', lab)
    print('%d failed' % bad if bad else 'ALL PASSED')
    return bad


if __name__ == '__main__':
    sys.exit(1 if main() else 0)
