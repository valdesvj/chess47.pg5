// test_web.js - web/engine.js against the C engine (build/jscheck from test/jscheck.c): for each position of
// test/web_positions.txt the perft count, the best move, its SAN, score, depth, node count, the Zobrist key and
// the FEN must be the C's exactly. Run: gcc -O2 -o build/jscheck test/jscheck.c src/engine.c && node test/test_web.js
'use strict';
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const ROOT = path.join(__dirname, '..');
require('./nnue_js.js');                               // the evaluation networks, if made
const E = require(path.join(ROOT, 'web', 'engine.js'));
const input = fs.readFileSync(path.join(__dirname, 'web_positions.txt'), 'utf8');
const want = execFileSync(path.join(ROOT, 'build', 'jscheck'), { input }).toString().trim().split('\n');
E.eng_init(1 << 20);
const hex = x => (x >>> 0).toString(16).padStart(8, '0');
let fails = 0, i = 0;
const t = Date.now();
for (const line of input.trim().split('\n')) {
  const [fen, d, pd, nz, he] = line.split('|');
  const p = E.newPos();
  E.chess960 = 0;
  if (!E.pos_from_fen(p, fen)) { console.log('BADFEN', fen); fails++; i++; continue; }
  const pf = E.perft(p, +pd);
  E.eng_clear_tt();
  const r = E.search(p, [[p.hi, p.lo]], nz ? { depth: +d, noise: +nz, seed: 12345, hand_eval: +(he || 0) } : { depth: +d });
  const got = [pf, E.move_to_str(p, r.best), E.move_to_san(p, r.best), r.score, r.depth, r.nodes,
               hex(p.hi) + hex(p.lo), E.pos_to_fen(p)].join(' ');
  const ok = got === want[i];
  if (!ok) fails++;
  console.log((ok ? 'ok    ' : 'FAIL  ') + got + (ok ? '' : '\n  C:   ' + want[i]));
  i++;
}
// the late move reductions: the JS table (Math.log) = src/lmr.h
const lmr = fs.readFileSync(path.join(ROOT, 'src', 'lmr.h'), 'utf8').match(/\{([\d,]+)\}/g).map(r => r.slice(1, -1).split(',').map(Number)).flat();
const okLmr = lmr.length === 32 * 64 && lmr.every((v, i) => v === E.LMR_T[i]);
console.log((okLmr ? 'ok    ' : 'FAIL  ') + 'LMR table = src/lmr.h');
if (!okLmr) fails++;
// the 960 numbering: 518 is normal chess
const q = E.newPos(); E.pos_960(q, 518);
const ok518 = E.pos_to_fen(q).startsWith('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha');
console.log((ok518 ? 'ok    ' : 'FAIL  ') + '960 #518 = ' + E.pos_to_fen(q));
if (!ok518) fails++;
console.log(fails ? fails + ' FAILED' : 'ALL OK', '(' + ((Date.now() - t) / 1000).toFixed(1) + ' s)');
process.exit(fails ? 1 : 0);
