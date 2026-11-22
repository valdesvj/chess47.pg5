#!/usr/bin/env python3
"""Puzzles 47 in the web version (web/chess47.html) in headless Chrome (Playwright): Shift+4 puts the game aside (the
saved game and settings in localStorage are not touched while the puzzles are on), a puzzle solved (the progress in
localStorage chess47.puzzles), a wrong move refused, the solver's other mate accepted, Shift+4 again: the game back;
after a reload too.

    pip install playwright && python3 test/test_web_puzzles.py [shots_dir]"""
import functools, http.server, json, os, sys, threading
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = sys.argv[1] if len(sys.argv) > 1 else None
bad = 0
FILES = 'abcdefgh'


def ok(cond, what, got=''):
    global bad
    bad += not cond
    print('  %s  %s%s' % ('ok  ' if cond else 'FAIL', what, '' if cond else '   got: %r' % (got,)))


class Cursor:
    KEYS = {(1, 1): '9', (-1, 1): '7', (1, -1): '3', (-1, -1): '1', (1, 0): '6', (-1, 0): '4', (0, 1): '8', (0, -1): '2'}

    def __init__(self, flip):
        self.flip, self.sq = flip, ('e7' if flip else 'e2')

    def go(self, sq):
        dx = FILES.index(sq[0]) - FILES.index(self.sq[0])
        dy = int(sq[1]) - int(self.sq[1])
        if self.flip:
            dx, dy = -dx, -dy
        k = []
        while dx or dy:
            sx, sy = (dx > 0) - (dx < 0), (dy > 0) - (dy < 0)
            k.append(self.KEYS[(sx, sy)]); dx -= sx; dy -= sy
        self.sq = sq
        return k

    def move(self, u):
        return self.go(u[:2]) + ['5'] + self.go(u[2:4]) + ['5']


def main():
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=os.path.join(ROOT, 'web'))
    httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    url = 'http://127.0.0.1:%d/chess47.html' % httpd.server_address[1]
    with sync_playwright() as pw:
        chrome = next((c for c in ('/usr/bin/google-chrome', '/usr/bin/google-chrome-stable', '/usr/bin/chromium')
                       if os.path.exists(c)), None)
        b = pw.chromium.launch(executable_path=chrome, args=['--no-sandbox'])
        page = b.new_context(viewport={'width': 1100, 'height': 900}).new_page()
        errors = []
        page.on('pageerror', lambda e: errors.append(str(e)))
        page.goto(url)
        page.wait_for_timeout(300)

        def keys(ks, gap=0):
            for k in ks:
                page.keyboard.press(k)
                if gap:
                    page.wait_for_timeout(gap)

        def saved(name):
            return json.loads(page.evaluate("localStorage.getItem('chess47.%s')" % name) or 'null')

        def shot(name):
            if SHOTS:
                page.locator('canvas').first.screenshot(path=os.path.join(SHOTS, name + '.png'))

        print('== a game: 1.e4 and the CPU\'s reply, then Shift+4')
        keys(['5', '8', '8', '5'])
        page.wait_for_timeout(2500)
        game = saved('auto')
        cfg0 = saved('cfg')
        ok(game and game['moves'][:1] == ['e2e4'] and len(game['moves']) == 2, 'the game saved: %s' % (game or {}).get('moves'), game)
        keys(['Shift+Digit4'])
        page.wait_for_timeout(300)
        shot('puzzle1')

        print('== puzzle 1 (Fool\'s mate): Qh4#')
        txt = page.evaluate('window.chess47_texts()')
        ok(txt[:2] == ['PUZZLES 47', '1/47'] and 'mate in 1' in txt, 'puzzle 1 shown', txt)
        keys(Cursor(True).move('d8h4'))
        page.wait_for_timeout(800)
        txt = page.evaluate('window.chess47_texts()')
        ok('Solved! 1 of 47' in txt, 'Solved! on the screen', txt)
        pz = saved('puzzles')
        ok(pz and pz['solved'][0] == 1 and pz['cur'] == 1, 'solved, kept in chess47.puzzles', pz)
        ok(saved('auto') == game, 'the saved game not touched', saved('auto'))
        c = saved('cfg')
        ok(all(c[k] == cfg0[k] for k in ('mode', 'human', 'flip', 'tc')), 'the saved settings not touched', c)
        shot('puzzle1_solved')

        print('== puzzle 2: Qxh7+ refused (no mate), then Qxf7# solved')
        keys(['F2'])
        cur = Cursor(False)
        keys(cur.move('h5h7'))
        page.wait_for_timeout(800)
        txt = page.evaluate('window.chess47_texts()')
        ok('No mate: try again' in txt and 'Qxh7+' not in txt, 'Qxh7+ refused, taken back', txt)
        ok(saved('puzzles')['solved'][1] == 0, 'Qxh7+ not a solution', saved('puzzles'))
        keys(cur.move('h5f7'))
        page.wait_for_timeout(800)
        ok(saved('puzzles')['solved'][1] == 1 and saved('puzzles')['cur'] == 2, 'Qxf7# solved', saved('puzzles'))

        print('== puzzle 36 (Lasker - Thomas): 14.f4+ is not the line\'s, but mates in time (the solver)')
        keys(['F1', 'F1'] + ['F2'] * 34)       # PREV twice: 2 -> 1 -> 47, NEXT round: 47 -> 1 ... -> 34
        page.wait_for_timeout(200)
        ok(saved('puzzles')['cur'] == 34, 'NEXT / PREV go round: puzzle %d' % saved('puzzles')['cur'], saved('puzzles'))
        keys(['F2'] * 2)
        cur = Cursor(False)
        for u in ('h5h7', 'e4f6', 'e5g4'):
            keys(cur.move(u)); page.wait_for_timeout(900)
        keys(cur.move('f2f4'))
        page.wait_for_timeout(4000)
        shot('puzzle36_f4')
        # the page's panel is a canvas: read the last move and the status from its text calls
        txt = page.evaluate('window.chess47_texts()')
        ok('f4+' in txt and 'Your move' in txt, '14.f4+ accepted, the defence played', txt)
        keys(['F4'])                           # SOLVE: the line played (and a check the page is alive)
        page.wait_for_timeout(500)

        print('== Shift+4 again: the game back; a reload too')
        keys(['Shift+Digit4'])
        page.wait_for_timeout(500)
        shot('back')
        ok(saved('auto')['moves'] == game['moves'], 'the game as it was: %s' % saved('auto')['moves'], saved('auto'))
        page.reload()
        page.wait_for_timeout(500)
        ok(saved('auto')['moves'] == game['moves'], 'after a reload too', saved('auto'))
        ok(saved('puzzles')['solved'][:2] == [1, 1], 'the progress kept', saved('puzzles'))
        ok(not errors, 'no page errors', errors)
        b.close()
    httpd.shutdown()
    print('all good' if not bad else '%d FAILED' % bad)
    return 1 if bad else 0


sys.exit(main())
