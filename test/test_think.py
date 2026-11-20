#!/usr/bin/env python3
"""The CPU's thinking display (SHIFT F6) in the PC simulator (build/sim): while it thinks the panel shows the expected
line and the counts (positions, a second, the time), its first move marked on the board; SHIFT F6 hides it. The
lines are checked legal with python-chess (when it is there). PAUSE:ms lets the search run before the snapshot.

    make sim && python3 test/test_think.py"""
import os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, 'build', 'sim')
bad = 0
L9 = ['F2'] * 5                          # LEVEL: 4 -> 9 (30 s a move)
A3 = ['4', '4', '4', '4', '5', '8', '5']  # 1. a3 (not in the book: the CPU searches)


def run(keys):
    d = tempfile.mkdtemp()
    subprocess.run([SIM] + keys, cwd=d, capture_output=True, timeout=180)
    out = {}
    for k in keys:
        if k.startswith('SNAP:'):
            n = k[5:]
            rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(d, n + '.txt')) if l.strip()]
            out[n] = [(int(r[2]), r[4]) for r in rows if int(r[1]) < 10 and int(r[2]) > 30]   # the panel's left column
            out[n + '.pbm'] = open(os.path.join(d, n + '.pbm')).read().split('\n')[2:]
    return out


def ok(cond, what, got):
    global bad
    bad += not cond
    print('  %s  %s%s' % ('ok  ' if cond else 'FAIL', what, '' if cond else '   got: %r' % (got,)))


STATS = re.compile(r'^\d+(\.\d)?[kM]?  \d+(\.\d)?[kM]?/s  \d+\.\ds$')


def think_block(panel):
    """(status, the line's moves, the counts) from the panel's texts, top down"""
    rows = [s for y, s in sorted(panel)]
    st = next((i for i, s in enumerate(rows) if STATS.match(s)), None)
    if st is None:
        return rows[0] if rows else '', [], None
    return rows[0], ' '.join(rows[1:st]).split(), rows[st]


def main():
    if not os.path.exists(SIM):
        sys.exit('make sim first')
    try:
        import chess
    except ImportError:
        chess = None

    print('== chess, level 9: the line and the counts while it thinks')
    r = run(L9 + A3 + ['PAUSE:6000', 'SNAP:a', 'PAUSE:6000', 'SNAP:b'])
    st, line, counts = think_block(r['a'])
    ok(re.match(r'^d\d+ \S+ [+-]\d+\.\d\d$', st), 'the status: depth, best move, score (%s)' % st, st)
    ok(len(line) >= 4, 'an expected line of 4 moves or more: %s' % ' '.join(line), line)
    ok(counts is not None, 'the counts: %s' % counts, r['a'])
    ok(line and st.split()[1] == line[0], 'the line starts with the best move', (st, line[:1]))
    if chess and line:
        b = chess.Board()
        b.push_san('a3')
        try:
            for m in line:
                b.push_san(m)
            ok(True, 'python-chess: the whole line is legal, in correct SAN', line)
        except ValueError as e:
            ok(False, 'python-chess: the whole line is legal, in correct SAN', (line, str(e)))
    st2, line2, counts2 = think_block(r['b'])
    ok(counts2 and counts2 != counts, '6 s later the counts moved on: %s' % counts2, (counts, counts2))

    print('== chess: after its move the panel is the move list again')
    r = run(L9[:1] + A3 + ['PAUSE:3000', 'SNAP:a'])      # level 5: one second
    st, line, counts = think_block(r['a'])
    ok(counts is None and any(s.startswith('1.') for y, s in r['a']), 'no thinking block, the moves', r['a'])

    print('== SHIFT F6 hides it (the status line only, as v1.1)')
    r = run(['SHIFT', 'F6', 'SNAP:t'] + L9 + A3 + ['PAUSE:5000', 'SNAP:a'])
    ok(any(s == 'Thinking hidden' for y, s in r['t']), 'Thinking hidden', r['t'])
    st, line, counts = think_block(r['a'])
    ok(counts is None and re.match(r'^d\d+ ', st), 'hidden: only the status line "dN move score"', r['a'])

    print('== Xiangqi: the line in WXF and the counts')
    r = run(['SHIFT', 'F1'] + L9 + ['5', '8', '5', 'PAUSE:6000', 'SNAP:a'])
    st, line, counts = think_block(r['a'])
    wxf = re.compile(r'^[KAEHRCPkaehrcp+-]?[1-9+-]?[+=.-][1-9]$|^[KAEHRCP][1-9][+=-][1-9]$')
    ok(len(line) >= 4 and all(wxf.match(m) for m in line), 'a WXF line of 4 moves or more: %s' % ' '.join(line), line)
    ok(counts is not None, 'the counts: %s' % counts, r['a'])

    print('%d failed' % bad if bad else 'all ok')
    sys.exit(1 if bad else 0)


main()
