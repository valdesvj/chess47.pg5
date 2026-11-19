#!/usr/bin/env python3
"""xq_check.py - the Xiangqi engine against another engine's move generator:
  1. perft 1-3 on positions reached by random games (checks, cannon screens, blocked horses and elephants, facing
     generals, soldiers over the river);
  2. with pyffish (Fairy-Stockfish's Python module) also the WXF notation of every legal move of those positions
     (test/xq_wxf.c prints the engine's).
The other engine: pyffish if it imports (pip install pyffish, in a venv on Arch), or a UCI engine binary that answers
"go perft N" and "d": Pikafish or Fairy-Stockfish (XQ_ENGINE=/path/to/pikafish, or --engine PATH); then no WXF check.
    make xq-test                                 (pyffish)
    make xq-test XQ_ENGINE=~/opt/Pikafish/src/pikafish
    python3 test/xq_check.py [positions] [--engine PATH]
(C) 2026 Victor Valdes - GPL-3.0-or-later"""
import os, random, subprocess, sys
try:
    import pyffish as sf
except ImportError:
    sf = None


class Uci:
    """Pikafish or Fairy-Stockfish over UCI: legal moves (perft 1's list), perft counts, the FEN after moves (d)."""
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.send('uci')
        self.until('uciok')
        self.send('setoption name UCI_Variant value xiangqi')       # Fairy-Stockfish; Pikafish ignores it
        self.send('isready'); self.until('readyok')

    def send(self, s):
        self.p.stdin.write(s + '\n'); self.p.stdin.flush()

    def until(self, word):
        lines = []
        while True:
            l = self.p.stdout.readline()
            if not l:
                raise SystemExit('the engine stopped')
            lines.append(l.strip())
            if l.startswith(word):
                return lines

    def perft(self, fen, d, moves=()):
        self.send('position fen %s%s' % (fen, (' moves ' + ' '.join(moves)) if moves else ''))
        self.send('go perft %d' % d)
        lines = self.until('Nodes searched')
        mv = [l.split(':')[0] for l in lines if ':' in l and not l.startswith(('Nodes', 'info'))]
        return int(lines[-1].split(':')[1]), mv

    def fen(self, fen, moves):
        self.send('position fen %s%s' % (fen, (' moves ' + ' '.join(moves)) if moves else ''))
        self.send('d')
        for l in self.until('Checkers'):
            if l.startswith('Fen:'):
                return l[4:].strip()
        raise SystemExit('no Fen: line from the engine')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
V = 'xiangqi'


START = 'rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1'


def positions_uci(eng, n, seed=47):
    rnd = random.Random(seed)
    out = []
    while len(out) < n:
        moves = []
        for ply in range(rnd.randint(4, 90)):
            cnt, legal = eng.perft(START, 1, moves)
            if not legal:
                break
            moves.append(rnd.choice(legal))
        out.append(eng.fen(START, moves))
    return out


def positions(n, seed=47):
    rnd = random.Random(seed)
    out = []
    while len(out) < n:
        fen, moves = sf.start_fen(V), []
        for ply in range(rnd.randint(4, 90)):
            legal = sf.legal_moves(V, fen, moves)
            if not legal:
                break
            caps = [m for m in legal if sf.is_capture(V, fen, moves, m)]
            moves.append(rnd.choice(caps) if caps and rnd.random() < 0.4 else rnd.choice(legal))
        out.append(sf.get_fen(V, fen, moves))
    return out


def perft(fen, d, moves=()):
    ms = sf.legal_moves(V, fen, list(moves))
    if d == 1:
        return len(ms)
    return sum(perft(fen, d - 1, list(moves) + [m]) for m in ms)


def main():
    args = [a for a in sys.argv[1:]]
    path = os.environ.get('XQ_ENGINE', '')
    if '--engine' in args:
        i = args.index('--engine'); path = args[i + 1]; del args[i:i + 2]
    n = int(args[0]) if args else 120
    eng = Uci(os.path.expanduser(path)) if path else None
    if not eng and not sf:
        raise SystemExit('no pyffish (pip install pyffish, in a venv on Arch) and no engine: '
                         'XQ_ENGINE=/path/to/pikafish or --engine PATH')
    print('the other engine: %s' % (path if eng else 'Fairy-Stockfish (pyffish)'))
    fens = positions_uci(eng, n) if eng else positions(n)
    exe = os.path.join(ROOT, 'build', 'xq_perft')
    jobs = [(d, f) for i, f in enumerate(fens) for d in ((1, 2, 3) if i < 5 else (1, 2))]
    r = subprocess.run([exe, '-'], input=''.join('%d %s\n' % j for j in jobs), text=True, capture_output=True)
    got = r.stdout.split()
    bad = 0
    for (d, f), g in zip(jobs, got):
        w = eng.perft(f, d)[0] if eng else perft(f, d)
        if str(w) != g:
            bad += 1
            if bad <= 10:
                print('PERFT FAIL d%d %s: engine %s, Fairy-Stockfish %d' % (d, f, g, w))
    print('perft: %d checks on %d positions, %d wrong' % (len(jobs), len(fens), bad))
    if eng:                                       # WXF: only with pyffish
        print('WXF: not checked (pyffish needed)')
        print('ALL PASS' if not bad else 'FAILED')
        sys.exit(1 if bad else 0)
    # WXF
    exe = os.path.join(ROOT, 'build', 'xq_wxf')
    r = subprocess.run([exe], input=''.join(f + '\n' for f in fens), text=True, capture_output=True)
    wbad = wn = 0
    for line in r.stdout.splitlines():
        fen, mv, wxf = line.rsplit(' ', 2)
        mv1 = mv[0] + str(int(mv[1:-2].rstrip('abcdefghi') or 0)) if False else None
        want = sf.get_san(V, fen, mv, False, sf.NOTATION_XIANGQI_WXF)
        wn += 1
        if want != wxf:
            wbad += 1
            if wbad <= 10:
                print('WXF FAIL %s %s: engine %s, Fairy-Stockfish %s' % (fen, mv, wxf, want))
    print('WXF: %d moves, %d different' % (wn, wbad))
    print('ALL PASS' if not bad and not wbad else 'FAILED')
    sys.exit(1 if bad or wbad else 0)


if __name__ == '__main__':
    main()
