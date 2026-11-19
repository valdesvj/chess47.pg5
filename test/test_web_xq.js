// test_web_xq.js - web/xq_engine.js against the C Xiangqi engine (build/xq_jscheck from test/xq_jscheck.c): on
// positions reached by random play from the start (a fixed seed) and a few set ones, the perft count, the best move,
// its WXF, score, depth, node count, the Zobrist key and the FEN must be the C's exactly. Run:
// gcc -O2 -o build/xq_jscheck test/xq_jscheck.c src/engine.c src/xq_engine.c && node test/test_web_xq.js
'use strict';
const path = require('path'), { execFileSync } = require('child_process');
const ROOT = path.join(__dirname, '..');
const X = require(path.join(ROOT, 'web', 'xq_engine.js'));
X.xq_init(1 << 20);
let seed = 1234567;
const rnd = n => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return (seed >>> 8) % n; };
const lines = [
  'rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1|6|4',
  'rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1|4|3|40',
  '3k5/9/9/9/9/9/9/9/4R4/4K4 w - - 0 1|7|3',
  '4k4/4a4/9/9/9/9/9/9/9/3RK4 w - - 0 1|7|3',
];
const ml = new Uint32Array(X.MAXMOVES);
for (let g = 0; g < 24; g++) {
  const p = X.newPos(), q = X.newPos();
  X.xq_pos_start(p);
  const plies = 6 + rnd(70);
  for (let i = 0; i < plies; i++) {
    const n = X.xq_gen_legal(p, ml);
    if (!n) break;
    X.xq_make_move(p, q, ml[rnd(n)]); X.copyPos(p, q);
  }
  if (!X.xq_gen_legal(p, ml)) continue;
  lines.push(X.xq_to_fen(p) + '|' + (4 + g % 3) + '|3' + (g % 5 === 0 ? '|25' : ''));
}
const input = lines.join('\n') + '\n';
const want = execFileSync(path.join(ROOT, 'build', 'xq_jscheck'), { input }).toString().trim().split('\n');
const hex = x => (x >>> 0).toString(16).padStart(8, '0');
let fails = 0;
const t = Date.now();
lines.forEach((line, i) => {
  const [fen, d, pd, nz] = line.split('|');
  const p = X.newPos();
  if (!X.xq_from_fen(p, fen)) { console.log('BADFEN', fen); fails++; return; }
  const pf = X.xq_perft(p, +pd);
  X.xq_clear_tt();
  const r = X.xq_search(p, [[p.hi, p.lo]], nz ? { depth: +d, noise: +nz, seed: 12345 } : { depth: +d });
  const got = [pf, X.xq_move_to_str(r.best, 0), X.xq_move_to_wxf(p, r.best), r.score, r.depth, r.nodes,
               hex(p.hi) + hex(p.lo), X.xq_to_fen(p)].join(' ');
  const ok = got === want[i];
  if (!ok) fails++;
  console.log((ok ? 'ok    ' : 'FAIL  ') + got + (ok ? '' : '\n  C:   ' + want[i]));
});
const s = X.newPos(); X.xq_pos_start(s);
const p4 = X.xq_perft(s, 4), ok4 = p4 === 3290240;
console.log((ok4 ? 'ok    ' : 'FAIL  ') + 'perft 4 from the start = ' + p4);
if (!ok4) fails++;
console.log(fails ? fails + ' FAILED' : 'ALL OK', '(' + ((Date.now() - t) / 1000).toFixed(1) + ' s)');
process.exit(fails ? 1 : 0);
