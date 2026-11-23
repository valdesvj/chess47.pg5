#!/usr/bin/env python3
"""The clock in the web version (web/chess47.html) in headless Chrome (Playwright): the same rules as the DM42n
(test/test_clock.py), read from the games the page saves in the browser (localStorage chess47.auto, chess47.xq:
the clock's times in ms, its control, stopped or not). Playwright's clock moves the page's time on.

    pip install playwright && python3 test/test_web_clock.py [shots_dir]"""
import functools, http.server, json, os, sys, threading
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = sys.argv[1] if len(sys.argv) > 1 else None
bad = 0

TWO = ['F6', 'F6', 'F2', 'F6']           # MODE (page 3): 2 players, back to page 1
E4 = ['5', '8', '8', '5']
E5 = ['8', '8', '8', '5', '2', '2', '5']
D4 = ['2', '2', '2', '4', '5', '8', '8', '5']
C2 = ['5', '4', '4', '4', '5']           # Xiangqi: the cannon h3-e3


def ok(cond, what, got):
    global bad
    bad += not cond
    print('  %s  %s%s' % ('ok  ' if cond else 'FAIL', what, '' if cond else '   got: %r' % (got,)))


def near(t, want, tol=1500):
    return t is not None and all(abs(a - b) <= tol for a, b in zip(t, want))


def main():
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=os.path.join(ROOT, 'web'))
    httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    url = 'http://127.0.0.1:%d/chess47.html' % httpd.server_address[1]
    with sync_playwright() as pw:
        chrome = next((c for c in ('/usr/bin/google-chrome', '/usr/bin/google-chrome-stable', '/usr/bin/chromium')
                       if os.path.exists(c)), None)
        b = pw.chromium.launch(executable_path=chrome, args=['--no-sandbox'])
        ctx = b.new_context(viewport={'width': 1100, 'height': 900})
        page = ctx.new_page()
        page.clock.install()
        page.goto(url)
        page.wait_for_timeout(300)

        def keys(ks):
            for k in ks:
                page.keyboard.press(k)

        def wait(ms):
            page.clock.fast_forward(ms)

        def saved(name):
            return json.loads(page.evaluate("localStorage.getItem('chess47.%s')" % name) or 'null')

        def shot(name):
            if SHOTS:
                page.locator('canvas').first.screenshot(path=os.path.join(SHOTS, name + '.png'))

        print('== web: 10+5 in chess, the increment, the first move free')
        keys(TWO + ['F6'] + ['F2'] * 4 + ['F6', 'F6'])
        keys(E4); wait(20000); keys(E5)
        s = saved('auto')
        ok(s['tc'] == 4 and near(s['clk'], [600000, 585000]), 'after e4 e5 (20 s): W 10:00, B 9:45',
           (s['tc'], s['clk']))
        wait(30000); keys(D4)
        s = saved('auto')
        ok(near(s['clk'], [575000, 585000]), 'after d4 (30 s): W 9:35', s['clk'])
        wait(7000)
        shot('web_1_running')
        keys(['F6', 'F2'])
        shot('web_2_ask')
        s = saved('auto')
        ok(not s.get('off'), 'the first CLOCK only asks', s.get('off'))
        keys(['F2'])
        shot('web_3_off')
        s = saved('auto')
        ok(s['off'] == 1 and near(s['clk'], [30000, 27000]), 'the second stops it: time used W 30 s, B 27 s',
           (s['off'], s['clk']))
        keys(['F2'])
        s = saved('auto')
        ok(s['off'] == 1 and s['tc'] == 4, 'a third press changes nothing (set at NEW)', (s['off'], s['tc']))

        print('== web: SHIFT F1, each game its own clock')
        keys(['F6', 'F6', 'Shift+F1'])
        keys(['F6', 'F2', 'F2', 'F6', 'F6'])                       # Xiangqi before its first move: 25+10
        keys(C2); wait(40000)
        shot('web_4_xiangqi')
        keys(['Shift+F1'])
        x, s = saved('xq'), saved('auto')
        ok(x['tc'] == 6 and near(x['clk'], [1500000, 1460000]), 'Xiangqi 25+10 kept: B 24:20', (x['tc'], x['clk']))
        ok(s['off'] == 1 and near(s['clk'], [30000, 27000]), 'chess back with its stopped clock, nothing refilled',
           (s['off'], s['clk']))
        wait(5000); keys(['Shift+F1'])
        s = saved('auto')
        ok(near(s['clk'], [30000, 32000]), 'chess counted up 5 s more for Black, then waited', s['clk'])

        print('== web: reload the page, both clocks come back')
        page.reload()
        page.wait_for_timeout(300)
        x = saved('xq')
        ok(x['tc'] == 6 and near(x['clk'], [1500000, 1460000]), 'Xiangqi after the reload: 25+10, B 24:20',
           (x['tc'], x['clk']))
        keys(['Shift+F1'])
        s = saved('auto')
        ok(s['off'] == 1 and near(s['clk'], [30000, 32000]), 'chess after the reload: stopped, W 0:30 B 0:32',
           (s['off'], s['clk']))
        shot('web_5_chess_reload')
        b.close()
    httpd.shutdown()
    print('%d failed' % bad if bad else 'all ok')
    sys.exit(1 if bad else 0)


main()
