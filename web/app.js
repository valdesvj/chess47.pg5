/*
 * CHESS47 web - the DM42n program's screen and keys in the browser (src/main.c, src/xq_ui.c): chess and
 * Xiangqi, the engines (engine.js, xq_engine.js) in a Web Worker. (C) 2026 Victor Valdes - GPL-3.0-or-later
 */
'use strict';
(function () {
const E = window.Chess47;
const { WHITE, BLACK, NOSQ, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, F_CAP, F_EP, F_CASTLE } = E;
const { MFROM, MTO, MPROMO, MFLAGS, PTYPE, PCOLOR, SQ, FILE_OF, RANK_OF, ONBOARD } = E;
const X = window.Xiangqi47, XB = window.XQ_BMP;

/* ---- layout: the DM42n's 400 x 240 LCD, drawn S times larger ---- */
const LCD_X = 400, LCD_Y = 240, S = 3;
/* chess: normal 26 px squares beside the panel, the soft keys below; zoom (+ / -) 30 px squares, the full height,
   no soft keys (they still work; SHIFT shows the game menu). Xiangqi: src/xq_ui.c's layout */
let SQPX, BX, BOARD_PX, PANEL_W, GLYPH, PANEL_EDGE;
const BY = 0;
const MENU_Y = 216, MENU_H = 24;
let XGAP, XB0, XY0, XPANEL, XR, XPS;
function layout() {
  SQPX = cfg.zoom ? 30 : 26; BX = cfg.zoom ? 160 : 192; BOARD_PX = 8 * SQPX; PANEL_W = BX - 6; GLYPH = cfg.zoom ? 26 : 20;
  XGAP = cfg.zoom ? 24 : 21; XB0 = cfg.zoom ? 196 : 222; XY0 = cfg.zoom ? 11 : 9; XPANEL = cfg.zoom ? 182 : 209;
  XR = cfg.zoom ? 12 : 11; XPS = cfg.zoom ? XB.size : XB.sizeN;
  PANEL_EDGE = xq ? XPANEL : BX - 2;
}
const LH = 21;                                            /* the t20 font's line height */
const FONT = `bold ${16 * S}px "DejaVu Sans Mono", "Menlo", "Consolas", monospace`;
const INK = '#22261e', PAPER = '#c7cdba';

const MODE_HC = 0, MODE_HH = 1, MODE_CC = 2, MODE_CNT = 3;
const MODE_NAME = ['You vs CPU', '2 players', 'CPU vs CPU'];
const LEVELS = [
  { name: 'CRAZY', depth: 0, ms: 0, noise: 0 },
  { name: '1', depth: 1, ms: 0, noise: 150 }, { name: '2', depth: 2, ms: 0, noise: 80 }, { name: '3', depth: 3, ms: 0, noise: 40 },
  { name: '4', depth: 5, ms: 500, noise: 6 }, { name: '5', depth: 40, ms: 1000, noise: 2 }, { name: '6', depth: 40, ms: 2500, noise: 0 },
  { name: '7', depth: 40, ms: 5000, noise: 0 }, { name: '8', depth: 40, ms: 10000, noise: 0 }, { name: '9', depth: 40, ms: 30000, noise: 0 },
];
const TCS = [
  { name: 'OFF', base: 0, inc: 0 }, { name: '1+0', base: 60, inc: 0 }, { name: '3+2', base: 180, inc: 2 }, { name: '5+0', base: 300, inc: 0 },
  { name: '10+0', base: 600, inc: 0 }, { name: '15+10', base: 900, inc: 10 }, { name: '30+0', base: 1800, inc: 0 }, { name: '60+0', base: 3600, inc: 0 },
];
const DM42N_NPS = 19;                                     /* nodes per ms of the DM42n on USB power (README) */
const MENU = [
  ['NEW', 'LEVEL', 'FLIP', 'UNDO', 'HINT', '>>'],
  ['960', 'CLOCK', 'SIDE', 'RESIGN', 'DRAW', '>>'],
  ['PGN', 'MODE', 'FEN', 'SAVE', 'LOAD', '>>'],
];
const PROMO_MENU = ['Queen', 'Rook', 'Bishop', 'Knight', '', 'Cancel'];
const BOOK = [
  'e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6',
  'e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4',
  'e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 d4c6 b7c6',
  'e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5',
  'e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1e3 e7e5',
  'e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5',
  'e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 d8c7',
  'e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7 e4e5 f6d7',
  'e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6',
  'e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6',
  'd2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8',
  'd2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5',
  'd2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5',
  'd2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5',
  'd2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7 f1g2 f8e7',
  'c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 d7d5',
  'c2c4 g8f6 b1c3 e7e6 e2e4 d7d5',
  'g1f3 d7d5 g2g3 g8f6 f1g2 e7e6 e1g1 f8e7 d2d3 e8g8',
  'g1f3 g8f6 c2c4 c7c5 b1c3 b8c6',
];
const BMP = window.CHESS47_BMP;                           /* src/pieces.h, made into JS by tools/build_web.py */

/* ---- state ---- */
const store = {
  get(k) { try { return JSON.parse(localStorage.getItem('chess47.' + k)); } catch (e) { return null; } },
  set(k, v) { try { localStorage.setItem('chess47.' + k, JSON.stringify(v)); } catch (e) { /* private window */ } },
};
const cfg = Object.assign({ level: 4, mode: MODE_HC, human: WHITE, flip: 0, tc: 0, sound: 1, page: 0, calc: 1,
                            game: 'chess', pstyle: 0, xstyle: 0, zoom: 0 }, store.get('cfg') || {});
let xq = cfg.game === 'xq' ? 1 : 0;                       /* the game on the screen: 0 chess, 1 Xiangqi */
let shift = 0;                                            /* SHIFT: the game menu on the soft keys */
const G = { hist: [E.newPos()], moves: [], san: [], keys: [], n: 0, over: 0, pos960: -1, legal: [], start: null, rfile: [7, 0], c960: 0 };
let cur = 0x14, sel = NOSQ, hint_from = NOSQ, hint_to = NOSQ, last_from = NOSQ, last_to = NOSQ;
let status = '', promo_mode = 0, promo_opts = [0, 0, 0, 0], thinking = 0, kirk = 0, info_screen = 0;
let resigner = -1, offer = -1, confirm = '', cpu_score = 0, cpu_scored = 0;   /* RESIGN, DRAW, the CPU's view */
let clk = [0, 0], turn_start = 0, clk_live = 0;
let last = { depth: 0, nodes: 0, ms: 0, score: 0 };
const CUR = () => G.hist[G.n];
/* Xiangqi's game (kept while chess is played, and the other way round) */
const XG = { start: '', hist: [], moves: [], wxf: [], keys: [], n: 0, over: 0, legal: [] };
const XCUR = () => XG.hist[XG.n];
let xseven = 0;                                           /* the Seven Stars (SHIFT 7) */
let xcur = 0, xsel = -1, xlast_from = -1, xlast_to = -1, xhint_from = -1, xhint_to = -1, xresigner = -1, xcpu_score = 0;
const side_now = () => xq ? XCUR().side : CUR().side;
const over_now = () => xq ? XG.over : G.over;
const now = () => performance.now();

E.eng_init(1 << 16);                                      /* the main thread: moves and notation only */

/* the engine's castling geometry belongs to the game (shared by the worker) */
function geometry() { E.c_rfile[0] = G.rfile[0]; E.c_rfile[1] = G.rfile[1]; E.chess960 = G.c960; }

/* ---- sound ---- */
let actx = null;
function beep(hz, ms) {
  if (!cfg.sound) return;
  try {
    actx = actx || new (window.AudioContext || window.webkitAudioContext)();
    const o = actx.createOscillator(), g = actx.createGain();
    o.type = 'square'; o.frequency.value = hz; g.gain.value = 0.04;
    o.connect(g); g.connect(actx.destination);
    const t = actx.currentTime + (beep.at = Math.max(beep.at || 0, 0));
    o.start(t); o.stop(t + ms / 1000);
  } catch (e) { /* no audio */ }
}

/* ---- clock ---- */
function clock_reset() { const t = TCS[cfg.tc]; clk = [t.base * 1000, t.base * 1000]; clk_live = 0; turn_start = now(); }
function clock_left(side) { return clk_live && side === side_now() ? clk[side] - (now() - turn_start) : clk[side]; }
function clock_update_live() { clk_live = cfg.tc && !over_now() && (xq ? XG.n : G.n) > 0 ? 1 : 0; if (clk_live) turn_start = turn_start || now(); }
function fmt_clock(ms) {
  if (ms < 0) ms = 0;
  const t = Math.floor((ms + 999) / 1000);
  if (t >= 3600) return `${Math.floor(t / 3600)}:${String(Math.floor(t / 60) % 60).padStart(2, '0')}:${String(t % 60).padStart(2, '0')}`;
  if (ms < 10000) return `${Math.floor(ms / 1000)}.${Math.floor(ms / 100) % 10}`;
  return `${Math.floor(t / 60)}:${String(t % 60).padStart(2, '0')}`;
}

/* ---- the game ---- */
function refresh_legal() {
  geometry();
  const ml = new Uint32Array(E.MAXMOVES), n = E.gen_legal(CUR(), ml);
  G.legal = Array.from(ml.subarray(0, n));
}
function new_game_from(start, pos960) {
  G.hist = [E.copyPos(E.newPos(), start)];
  G.rfile = [E.c_rfile[0], E.c_rfile[1]]; G.c960 = E.chess960;
  G.start = E.pos_to_fen(start);
  G.moves = []; G.san = []; G.keys = [[start.hi, start.lo]]; G.n = 0; G.over = 0; G.pos960 = pos960;
  kirk = 0; resigner = offer = -1; confirm = ''; cpu_scored = 0;
  sel = hint_from = hint_to = last_from = last_to = NOSQ;
  cur = cfg.flip ? 0x64 : 0x14;
  status = ''; promo_mode = 0;
  clock_reset();
  refresh_legal();
  worker_call({ cmd: 'newgame' });
}
function new_game(pos960) {
  const p = E.newPos();
  if (pos960 < 0) E.pos_start(p); else E.pos_960(p, pos960);
  new_game_from(p, pos960);
  save_auto();
}

const result_text = (code, loser) => {
  if (kirk && code === 1) return loser === WHITE ? 'Kirk wins  0-1' : 'Kirk wins  1-0';
  if (code === 7 && resigner >= 0) loser = resigner;
  if (code === 9) return 'Draw agreed 1/2';
  return ['', loser === WHITE ? 'Checkmate  0-1' : 'Checkmate  1-0', 'Stalemate  1/2', '50 moves  1/2', 'No material 1/2',
          'Repetition 1/2', loser === WHITE ? 'W flag fell 0-1' : 'B flag fell 1-0', loser === WHITE ? 'W resigns 0-1' : 'B resigns 1-0'][code] || '';
};
const result_tag = () => {
  const loser = G.over === 7 && resigner >= 0 ? resigner : CUR().side;
  if ([1, 6, 7].includes(G.over)) return loser === WHITE ? '0-1' : '1-0';
  if ([2, 3, 4, 5, 9].includes(G.over)) return '1/2-1/2';
  return '*';
};

function check_over() {
  geometry();
  const st = E.game_state(CUR(), G.keys);
  if (st) { G.over = st; clk_live = 0; beep(st === 1 ? 440 : 660, 250); }
}

function do_move(m) {
  if (G.n >= E.MAXGAME) { status = 'Game too long'; return; }
  geometry();
  const p = CUR(), mover = p.side;
  if (clk_live) {
    clk[mover] -= now() - turn_start;
    if (clk[mover] <= 0) { clk[mover] = 0; G.over = 6; clk_live = 0; beep(330, 400); return; }
    clk[mover] += TCS[cfg.tc].inc * 1000;
  }
  turn_start = now();
  if (offer >= 0 && mover !== offer) offer = -1;            /* 2 players: moving declines the other side's offer */
  G.san[G.n] = E.move_to_san(p, m);
  const q = E.newPos();
  E.make_move(p, q, m);
  G.hist[G.n + 1] = q;
  G.moves[G.n] = m;
  G.n++;
  G.hist.length = G.n + 1; G.moves.length = G.n; G.san.length = G.n;
  G.keys[G.n] = [q.hi, q.lo]; G.keys.length = G.n + 1;
  last_from = MFROM(m); last_to = MTO(m);
  sel = hint_from = hint_to = NOSQ;
  status = '';
  refresh_legal();
  check_over();
  clock_update_live();
  beep(MFLAGS(m) & F_CAP ? 1400 : 1000, 18);
  save_auto();
}

const is_human = side => cfg.mode === MODE_HH ? 1 : cfg.mode === MODE_CC ? 0 : side === cfg.human ? 1 : 0;

function undo() {
  if (!G.n || thinking) return;
  let plies = 1;
  if (cfg.mode === MODE_HC && G.n >= 2 && !is_human(G.hist[G.n - 1].side)) plies = 2;
  if (cfg.mode === MODE_HC && G.n === 1 && !is_human(G.hist[0].side)) plies = 1;
  G.n = Math.max(0, G.n - plies);
  G.hist.length = G.n + 1; G.moves.length = G.n; G.san.length = G.n; G.keys.length = G.n + 1;
  G.over = 0;
  sel = hint_from = hint_to = last_from = last_to = NOSQ;
  if (G.n) { last_from = MFROM(G.moves[G.n - 1]); last_to = MTO(G.moves[G.n - 1]); }
  status = '';
  refresh_legal();
  turn_start = now();
  clock_update_live();
  save_auto();
}

/* ---- the engine in a Web Worker (the page stays responsive; EXIT: move now) ---- */
let worker = null, wjob = null;
function worker_make() {
  const src = document.getElementById('engine-src').textContent + '\n;(' + worker_main.toString() + ')();';
  worker = new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
  worker.onmessage = ev => {
    const d = ev.data;
    if (d.type === 'info' && wjob) { wjob.best = d.best; wjob.score = d.score; wjob.depth = d.depth; wjob.nodes = d.nodes; if (wjob.oninfo) wjob.oninfo(d); }
    if (d.type === 'done' && wjob) { const j = wjob; wjob = null; j.resolve(d); }
  };
}
function worker_main() {                                  /* runs inside the worker */
  const E = self.Chess47, X = self.Xiangqi47;
  E.eng_init(1 << 20);
  let xready = 0;
  self.onmessage = ev => {
    const d = ev.data;
    if (d.cmd === 'newgame') { E.eng_clear_tt(); if (xready) X.xq_clear_tt(); return; }
    if (d.cmd === 'xsearch') {                            /* Xiangqi: its own tables (2^20 entries, as chess's) */
      if (!xready) { X.xq_init(1 << 20); xready = 1; }
      const p = X.newPos();
      X.xq_from_fen(p, d.start);
      const keys = [[p.hi, p.lo]];
      let q = p;
      for (const m of d.moves) { const n = X.newPos(); X.xq_make_move(q, n, m); q = n; keys.push([q.hi, q.lo]); }
      const t0 = performance.now();
      const r = X.xq_search(q, keys, {
        depth: d.depth, time_ms: d.ms, noise: d.noise, seed: d.seed, max_nodes: d.max_nodes, now: () => performance.now(),
        info: (depth, score, nodes, best) => self.postMessage({ type: 'info', depth, score, nodes, best, san: X.xq_move_to_wxf(q, best) }),
      });
      self.postMessage({ type: 'done', best: r.best, score: r.score, depth: r.depth, nodes: r.nodes, ms: performance.now() - t0 });
      return;
    }
    if (d.cmd !== 'search') return;
    E.c_rfile[0] = d.rfile[0]; E.c_rfile[1] = d.rfile[1]; E.chess960 = d.c960;
    const p = E.newPos();
    E.pos_from_fen(p, d.start);
    E.c_rfile[0] = d.rfile[0]; E.c_rfile[1] = d.rfile[1]; E.chess960 = d.c960;
    const keys = [[p.hi, p.lo]];
    let q = p;
    for (const m of d.moves) { const n = E.newPos(); E.make_move(q, n, m); q = n; keys.push([q.hi, q.lo]); }
    const t0 = performance.now();
    const r = E.search(q, keys, {
      depth: d.depth, time_ms: d.ms, noise: d.noise, seed: d.seed, max_nodes: d.max_nodes, now: () => performance.now(),
      info: (depth, score, nodes, best) => self.postMessage({ type: 'info', depth, score, nodes, best, san: E.move_to_san(q, best) }),
    });
    self.postMessage({ type: 'done', best: r.best, score: r.score, depth: r.depth, nodes: r.nodes, ms: performance.now() - t0 });
  };
}
function worker_call(msg) { if (!worker) worker_make(); worker.postMessage(msg); }
function run_search(depth, ms, noise, oninfo) {
  return new Promise(resolve => {
    wjob = { resolve, oninfo, best: 0 };
    const max_nodes = cfg.calc ? Math.max(1, (ms || 0) * DM42N_NPS) : 0;
    worker_call(xq ? { cmd: 'xsearch', start: XG.start, moves: XG.moves.slice(0, XG.n), depth, ms: cfg.calc && ms ? 0 : ms,
                       noise, seed: (Math.random() * 4294967296) >>> 0, max_nodes: cfg.calc && ms ? max_nodes : 0 } :
                {     cmd: 'search', start: G.start, rfile: G.rfile, c960: G.c960, moves: G.moves.slice(0, G.n),
                  depth, ms: cfg.calc && ms ? 0 : ms, noise, seed: (Math.random() * 4294967296) >>> 0,
                  max_nodes: cfg.calc && ms ? max_nodes : 0 });
  });
}
function move_now_discard() {                             /* stop thinking, no move (resign, new game) */
  if (!wjob) return;
  const j = wjob; wjob = null; thinking = 0; worker.terminate(); worker = null;
  j.resolve({ best: 0, discard: 1 });
}
function move_now() {                                     /* EXIT while thinking: the best move so far */
  if (!wjob) return;
  const j = wjob; wjob = null;
  worker.terminate(); worker = null;
  j.resolve({ best: j.best, score: j.score || 0, depth: j.depth || 0, nodes: j.nodes || 0, ms: 0, stopped: 1 });
}

function think_time(level_ms) {
  if (!cfg.tc) return level_ms;
  const left = clock_left(side_now());
  let t = left / 25 + TCS[cfg.tc].inc * 750;
  if (t > left / 3) t = left / 3;
  if (t < 50) t = 50;
  if (level_ms && t > level_ms) t = level_ms;
  return Math.floor(t);
}

function book_move() {
  if (G.c960 || G.pos960 >= 0 || G.start !== 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1' || G.n >= 14 || kirk) return 0;
  geometry();
  let line = '';
  for (let i = 0; i < G.n; i++) {
    const m = G.moves[i];
    let s = E.move_to_str(G.hist[i], m).slice(0, 4);
    if (MFLAGS(m) & F_CASTLE) s = s.slice(0, 2) + E.sqname(MTO(m));
    line += s + ' ';
  }
  const cand = [];
  for (const b of BOOK) if (b.startsWith(line) && b.length >= line.length + 4) cand.push(b.substr(line.length, 4));
  if (!cand.length) return 0;
  return E.str_to_move(CUR(), cand[Math.floor(Math.random() * cand.length)]);
}

const sleep = ms => new Promise(r => setTimeout(r, ms));
async function cpu_move() {
  const L = LEVELS[cfg.level];
  let m;
  if (cfg.level >= 4 && (m = book_move())) {
    await sleep(200);
    do_move(m); status = 'Book: ' + G.san[G.n - 1]; return;
  }
  if (cfg.level === 0) {
    await sleep(250);
    m = G.legal[Math.floor(Math.random() * G.legal.length)];
  } else {
    thinking = 1; status = 'Thinking...'; redraw();
    const n0 = G.n;
    const r = await run_search(L.depth, think_time(L.ms), L.noise, d => {
      status = E.IS_MATE(d.score) ? `d${d.depth} ${d.san} M${d.score < 0 ? '-' : ''}${Math.floor((E.MATE - Math.abs(d.score) + 1) / 2)}`
                                  : `d${d.depth} ${d.san} ${d.score >= 0 ? '+' : '-'}${Math.floor(Math.abs(d.score) / 100)}.${String(Math.abs(d.score) % 100).padStart(2, '0')}`;
      redraw();
    });
    thinking = 0;
    if (r.discard || G.n !== n0 || G.over) return;        /* stopped (resign, draw, new game) */
    last = { depth: r.depth, nodes: r.nodes, ms: r.ms, score: r.score };
    m = r.best || G.legal[0];
    cpu_score = r.score; cpu_scored = 1;
  }
  do_move(m);
  if (cfg.level === 0 && !G.over) status = 'CRAZY played ' + G.san[G.n - 1];
}

async function hint() {
  if (G.over || !G.legal.length || thinking) return;
  thinking = 1; status = 'Thinking...'; redraw();
  const n0 = G.n;
  const r = await run_search(40, 1500, 0);
  thinking = 0;
  if (r.discard || G.n !== n0 || !r.best) return;
  geometry();
  hint_from = MFROM(r.best);
  hint_to = MFLAGS(r.best) & F_CASTLE ? E.castle_rook_sq(CUR(), r.best) : MTO(r.best);
  status = 'Hint: ' + E.move_to_san(CUR(), r.best);
}

/* ---- the 960 number, and the Easter egg ---- */
function kobayashi_maru() {                               /* 960 # 1701 (NCC-1701): Kirk reprogrammed the test */
  const p = E.newPos();
  E.chess960 = 0;
  E.pos_from_fen(p, cfg.human === BLACK && cfg.mode === MODE_HC ? 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/4K3 w kq - 0 1'
                                                               : '4k3/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQ - 0 1');
  new_game_from(p, -1);
  kirk = 1;
  status = 'KIRK RULES!';
  beep(1047, 120); setTimeout(() => beep(1319, 120), 130); setTimeout(() => beep(1568, 240), 260);
  save_auto();
}
function ask_960() {
  const dlg = document.getElementById('n960');
  const inp = dlg.querySelector('input');
  inp.value = '';
  dlg.showModal();
  inp.focus();
  dlg.onclose = () => {
    if (dlg.returnValue === 'cancel') return;
    const v = inp.value.trim();
    const n = v === '' || dlg.returnValue === 'random' ? Math.floor(Math.random() * 960) : parseInt(v, 10);
    if (n === 1701) kobayashi_maru();
    else if (!(n >= 0 && n <= 959)) status = '960 # must be 0-959';
    else { new_game(n); status = 'Chess960 #' + n; }
    redraw();
  };
}

/* ---- files: PGN, FEN, save / load (this browser) ---- */
function build_pgn() {
  const d = new Date(), lvl = 'Chess 47 L' + LEVELS[cfg.level].name;
  const w = cfg.mode === MODE_HH || (cfg.mode === MODE_HC && cfg.human === WHITE) ? 'Human' : lvl;
  const b = cfg.mode === MODE_HH || (cfg.mode === MODE_HC && cfg.human === BLACK) ? 'Human' : lvl;
  let o = `[Event "Chess 47 web"]\n[Site "Chess 47 web (the DM42n's engine)"]\n[Date "${d.getFullYear()}.${String(d.getMonth() + 1).padStart(2, '0')}.${String(d.getDate()).padStart(2, '0')}"]\n`;
  o += `[White "${w}"]\n[Black "${b}"]\n[Result "${result_tag()}"]\n`;
  const h0 = G.hist[0];
  if (G.c960 || h0.fullmove !== 1 || h0.side !== WHITE) o += (G.c960 ? '[Variant "Chess960"]\n' : '') + `[SetUp "1"]\n[FEN "${G.start}"]\n`;
  if (cfg.tc) o += `[TimeControl "${TCS[cfg.tc].base}+${TCS[cfg.tc].inc}"]\n`;
  o += '\n';
  let col = 0;
  for (let i = 0; i < G.n; i++) {
    const p = G.hist[i];
    const tok = p.side === WHITE ? `${p.fullmove}. ${G.san[i]}` : i === 0 ? `${p.fullmove}... ${G.san[i]}` : G.san[i];
    if (col + tok.length > 78) { o += '\n'; col = 0; } else if (col) { o += ' '; col++; }
    o += tok; col += tok.length;
  }
  return o + (col ? ' ' : '') + result_tag() + '\n';
}
function download(name, text) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([text], { type: 'application/x-chess-pgn' }));
  a.download = name; document.body.appendChild(a); a.click(); a.remove();
}
function copy(text) { try { navigator.clipboard.writeText(text); return 1; } catch (e) { return 0; } }

function serialize() {
  geometry();
  return { start: G.start, rfile: G.rfile, c960: G.c960, pos960: G.pos960, kirk, resigner,
           moves: G.moves.slice(0, G.n).map((m, i) => E.move_to_str(G.hist[i], m)), over: G.over === 7 || G.over === 9 ? G.over : 0, clk };
}
function deserialize(s) {
  if (!s || !s.start) return 0;
  E.c_rfile[0] = s.rfile[0]; E.c_rfile[1] = s.rfile[1]; E.chess960 = s.c960;
  const p = E.newPos();
  if (!E.pos_from_fen(p, s.start)) return 0;
  E.c_rfile[0] = s.rfile[0]; E.c_rfile[1] = s.rfile[1]; E.chess960 = s.c960;
  new_game_from(p, s.pos960);
  kirk = s.kirk || 0;
  for (const u of s.moves) { const m = E.str_to_move(CUR(), u); if (!m) break; do_move(m); }
  if (s.over === 7 || s.over === 9) { G.over = s.over; resigner = s.resigner ?? -1; clk_live = 0; }
  if (s.clk && cfg.tc) { clk = s.clk.slice(); turn_start = now(); }
  last_from = last_to = NOSQ;
  if (G.n) { last_from = MFROM(G.moves[G.n - 1]); last_to = MTO(G.moves[G.n - 1]); }
  status = '';
  return 1;
}
function save_auto() {
  if (G.hist.length && G.start) store.set('auto', serialize());
  if (XG.hist.length) store.set('xq', xq_serialize());
  store.set('cfg', cfg);
}

/* ---- keys ---- */
function move_cursor(dx, dy) {
  if (cfg.flip) { dx = -dx; dy = -dy; }
  cur = SQ((FILE_OF(cur) + dx + 8) % 8, (RANK_OF(cur) + dy + 8) % 8);
}
const has_moves_from = sq => G.legal.some(m => MFROM(m) === sq);
function select_square() {
  if (G.over || !is_human(CUR().side) || thinking) return;
  geometry();
  const pc = CUR().b[cur];
  if (sel === NOSQ) {
    if (pc && PCOLOR(pc) === CUR().side && has_moves_from(cur)) sel = cur; else beep(220, 40);
    return;
  }
  if (cur === sel) { sel = NOSQ; return; }
  const normal_to = G.legal.some(m => MFROM(m) === sel && MTO(m) === cur && !(MFLAGS(m) & F_CASTLE));
  const cand = [];
  for (const m of G.legal) {
    if (MFROM(m) !== sel) continue;
    if (MFLAGS(m) & F_CASTLE) { if (E.castle_rook_sq(CUR(), m) === cur || (MTO(m) === cur && !normal_to)) cand.push(m); }
    else if (MTO(m) === cur) cand.push(m);
  }
  if (!cand.length) {
    if (pc && PCOLOR(pc) === CUR().side && has_moves_from(cur)) sel = cur; else beep(220, 40);
    return;
  }
  if (cand.length > 1 && MPROMO(cand[0])) {
    promo_mode = 1; promo_opts = [0, 0, 0, 0];
    for (const m of cand) { const pr = MPROMO(m); promo_opts[pr === QUEEN ? 0 : pr === ROOK ? 1 : pr === BISHOP ? 2 : 3] = m; }
    return;
  }
  do_move(cand[0]);
}

/* RESIGN (twice; Shift+Backspace at once): against the CPU you resign, in 2 players the side to move */
function resign(now_) {
  if (G.over) { status = 'Game over'; return; }
  if (cfg.mode === MODE_CC) { status = 'Demo: NEW or MODE'; return; }
  if (!now_ && confirm !== 'R') { confirm = 'R'; status = 'Resign? RESIGN again'; return; }
  confirm = '';
  if (thinking) move_now_discard();
  resigner = cfg.mode === MODE_HC ? cfg.human : CUR().side;
  G.over = 7; clk_live = 0; beep(330, 300); save_auto();
}
/* DRAW: the CPU accepts when it is worse (its last score <= -0.30), or from move 20 when not better (<= 0); in 2 players the other side ACCEPTs (F5 on
   page 1, or HINT's key while the offer stands) on its turn, and moving declines */
function draw() {
  if (G.over) { status = 'Game over'; return; }
  if (cfg.mode === MODE_CC) { status = 'Demo: no draw'; return; }
  if (cfg.mode === MODE_HH) {
    if (offer === (CUR().side ^ 1)) {
      if (confirm !== 'A') { confirm = 'A'; status = 'Draw? ACCEPT again'; return; }
      confirm = ''; offer = -1; G.over = 9; clk_live = 0; beep(660, 250); save_auto(); return;
    }
    if (offer === CUR().side) { status = 'Offered: now move'; return; }
    offer = CUR().side; status = (offer ? 'Black' : 'White') + ' offers a draw';
    return;
  }
  geometry();
  const sc = cpu_scored ? cpu_score : (CUR().side === cfg.human ? -1 : 1) * E.evaluate(CUR());
  if (!(sc <= -30 || (sc <= 0 && G.n >= 40))) { status = 'CPU declines the draw'; beep(220, 80); return; }
  if (thinking) move_now_discard();
  G.over = 9; clk_live = 0; beep(660, 250); save_auto();
}
const accept_pending = () => cfg.mode === MODE_HH && offer === (CUR().side ^ 1) && !G.over;

function softkey(fn) {
  if (info_screen) { info_screen = 0; return; }
  if (shift && !promo_mode) { game_menu(fn); return; }
  if (xq) { xq_softkey(fn); return; }
  if (promo_mode) {
    if (fn < 4 && promo_opts[fn]) { promo_mode = 0; do_move(promo_opts[fn]); }
    else if (fn === 5) { promo_mode = 0; sel = NOSQ; }
    return;
  }
  const k = cfg.page * 6 + fn;
  if (!((k === 9 && confirm === 'R') || ((k === 10 || k === 4) && confirm === 'A') || (k === 0 && confirm === 'N'))) confirm = '';
  if (fn === 5) { cfg.page = (cfg.page + 1) % 3; store.set('cfg', cfg); return; }
  if (thinking && !(k === 0 || k === 9 || k === 10)) { status = 'Thinking... (Esc: move now)'; return; }
  switch (k) {
  case 0:
    if (cfg.mode === MODE_HC && !G.over && G.n >= 2) {   /* leaving a game against the CPU loses it */
      if (confirm !== 'N') { confirm = 'N'; status = 'Quit=resign? NEW'; break; }
      resign(1); break;                                  /* the result shows; NEW again for the next game */
    }
    if (thinking) move_now_discard();
    new_game(G.pos960 >= 0 ? Math.floor(Math.random() * 960) : -1); break;
  case 1: cfg.level = (cfg.level + 1) % 10; status = 'Level ' + LEVELS[cfg.level].name; break;
  case 2: cfg.flip ^= 1; break;
  case 3: undo(); break;
  case 4: if (accept_pending()) draw(); else hint(); break;
  case 6: if (G.c960) { new_game(-1); status = 'Normal chess'; } else ask_960(); break;
  case 7: cfg.tc = (cfg.tc + 1) % TCS.length; clock_reset(); clock_update_live(); status = 'Clock ' + TCS[cfg.tc].name; break;
  case 8: cfg.human ^= 1; cfg.flip = cfg.human; if (cfg.mode !== MODE_HC) cfg.mode = MODE_HC; sel = NOSQ;
          status = 'You play ' + (cfg.human ? 'Black' : 'White'); break;
  case 9: resign(0); break;
  case 10: draw(); break;
  case 15: store.set('game', serialize()); status = 'Saved'; break;
  case 16: status = deserialize(store.get('game')) ? 'Loaded' : 'No saved game'; break;
  case 12: download('GAME.PGN', build_pgn()); status = copy(build_pgn()) ? 'PGN saved, copied' : 'PGN saved'; break;
  case 13: cfg.mode = (cfg.mode + 1) % MODE_CNT; sel = NOSQ; status = MODE_NAME[cfg.mode]; break;
  case 14: status = copy(E.pos_to_fen(CUR())) ? 'FEN copied' : 'FEN: see the console'; console.log(E.pos_to_fen(CUR())); break;
  }
  store.set('cfg', cfg);
}

function key(k, shifted) {
  if (info_screen) { info_screen = 0; return; }
  if (shifted && k === 'Backspace') { shift = 0; if (xq) xq_resign(1); else resign(1); return; }
  if (k === 'i') { info_screen = 1; return; }
  if (k === 'Escape' && shift) { shift = 0; return; }
  if (shift && k === '7') { shift = 0; xq_seven(); return; }
  shift = 0;
  if (promo_mode) { if (k === 'Escape') { promo_mode = 0; sel = NOSQ; } return; }
  if (k === '*' || k === 'x') { piece_style(); return; }
  if (k === '+' || k === '-') { zoom_toggle(); return; }
  if (xq) { xq_key(k); return; }
  status = thinking ? status : '';
  switch (k) {
  case 'ArrowUp': case '8': move_cursor(0, 1); break;
  case 'ArrowDown': case '2': move_cursor(0, -1); break;
  case 'ArrowLeft': case '4': move_cursor(-1, 0); break;
  case 'ArrowRight': case '6': move_cursor(1, 0); break;
  case '7': move_cursor(-1, 1); break;
  case '9': move_cursor(1, 1); break;
  case '1': move_cursor(-1, -1); break;
  case '3': move_cursor(1, -1); break;
  case '5': case 'Enter': case ' ': select_square(); break;
  case 'Escape': if (thinking) move_now(); sel = NOSQ; hint_from = hint_to = NOSQ; break;
  case 'Backspace': undo(); break;
  }
}

/* ======== Xiangqi (src/xq_ui.c): the same levels, clock, modes and keys; WXF moves ======== */
const XQ_MENU = [
  ['NEW', 'LEVEL', 'FLIP', 'UNDO', 'HINT', '>>'],
  ['PIECES', 'CLOCK', 'SIDE', 'RESIGN', 'DRAW', '>>'],
  ['CHESS', 'MODE', 'FEN', '', '', '>>'],
];
const XQ_START = 'rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1';
const XQ_STYLE = ['Pieces: characters', 'Pieces: symbols', 'Pieces: letters'];
X.xq_init(1 << 10);                                       /* the main thread: moves and notation only */

function xq_refresh_legal() {
  const ml = new Uint32Array(X.MAXMOVES), n = X.xq_gen_legal(XCUR(), ml);
  XG.legal = Array.from(ml.subarray(0, n));
}
function xq_new_game_from(fen) {
  const p = X.newPos();
  if (!X.xq_from_fen(p, fen)) return 0;
  XG.start = fen; XG.hist = [p]; XG.moves = []; XG.wxf = []; XG.keys = [[p.hi, p.lo]]; XG.n = 0; XG.over = 0;
  xsel = xhint_from = xhint_to = xlast_from = xlast_to = -1;
  xresigner = offer = -1; confirm = ''; xcpu_score = 0;
  xcur = X.XSQ(7, 2);                                     /* Red's cannon: C2=5 is the most common first move */
  xseven = fen === XQ_SEVEN ? 1 : 0;
  status = ''; clock_reset(); xq_refresh_legal();
  worker_call({ cmd: 'newgame' });
  return 1;
}
function xq_new_game() { xq_new_game_from(XQ_START); save_auto(); }

/* SHIFT 7 (七, seven): 七星聚會, the Seven Stars Gathering, the king of the classical endgames, as 竹香齋 (1801) sets
   it: Red 帥 e1, 俥 g1 h1, 炮 h3, 兵 d9 f8 i5; Black 將 f10, 車 e10, 象 e8, 卒 b3 e3 d2 f2. Red to move; with the best
   play a draw. The famous line: 炮二平四 卒5平6 兵四進一 將6進1 俥三進八 將6退1 ... */
const XQ_SEVEN = '4rk3/3P5/4bP3/9/9/8P/9/1p2p2C1/3p1p3/4K1RR1 w - - 0 1';
function xq_seven() {
  if (thinking) move_now_discard();
  if (!xq) { xq = 1; cfg.game = 'xq'; offer = -1; }
  xq_new_game_from(XQ_SEVEN);
  status = 'Seven Stars 1801';
  beep(784, 120); setTimeout(() => beep(988, 120), 130); setTimeout(() => beep(1175, 120), 260); setTimeout(() => beep(1568, 300), 390);
  save_auto();
}

function xq_result(code) {
  const loser = code === 7 ? xresigner : XCUR().side;
  switch (code) {
  case 1: return loser === X.XRED ? 'Checkmate  0-1' : 'Checkmate  1-0';
  case 2: return loser === X.XRED ? 'No moves  0-1' : 'No moves  1-0';      /* stalemate loses in Xiangqi */
  case 3: return '60 moves  1/2';
  case 4: return 'No attackers 1/2';
  case 5: return 'Repetition 1/2';
  case 6: return loser === X.XRED ? 'Red flag fell 0-1' : 'Black flag 1-0';
  case 7: return loser === X.XRED ? 'Red resigns 0-1' : 'Black resigns 1-0';
  case 8: return 'Draw agreed 1/2';
  }
  return '';
}

function xq_do_move(m) {
  if (XG.n >= X.MAXGAME) { status = 'Game too long'; return; }
  const p = XCUR(), mover = p.side;
  if (clk_live) {
    clk[mover] -= now() - turn_start;
    if (clk[mover] <= 0) { clk[mover] = 0; XG.over = 6; clk_live = 0; beep(330, 400); return; }
    clk[mover] += TCS[cfg.tc].inc * 1000;
  }
  turn_start = now();
  if (offer >= 0 && mover !== offer) offer = -1;            /* moving declines the other side's draw offer */
  XG.wxf[XG.n] = X.xq_move_to_wxf(p, m);
  const q = X.newPos();
  X.xq_make_move(p, q, m);
  XG.moves[XG.n] = m;
  XG.n++;
  XG.hist[XG.n] = q;
  XG.hist.length = XG.n + 1; XG.moves.length = XG.n; XG.wxf.length = XG.n;
  XG.keys[XG.n] = [q.hi, q.lo]; XG.keys.length = XG.n + 1;
  xlast_from = X.XFROM(m); xlast_to = X.XTO(m);
  xsel = xhint_from = xhint_to = -1;
  status = '';
  xq_refresh_legal();
  const st = X.xq_game_state(XCUR(), XG.keys);
  if (st) { XG.over = st; clk_live = 0; beep(st <= 2 ? 440 : 660, 250); }
  clock_update_live();
  beep(X.XFLAGS(m) & X.XF_CAP ? 1400 : 1000, 18);
  save_auto();
}

function xq_undo() {
  if (!XG.n || thinking) return;
  let plies = 1;
  if (cfg.mode === MODE_HC && XG.n >= 2 && !is_human(XG.hist[XG.n - 1].side)) plies = 2;
  XG.n = Math.max(0, XG.n - plies);
  XG.hist.length = XG.n + 1; XG.moves.length = XG.n; XG.wxf.length = XG.n; XG.keys.length = XG.n + 1;
  XG.over = 0;
  xsel = xhint_from = xhint_to = xlast_from = xlast_to = -1;
  if (XG.n) { xlast_from = X.XFROM(XG.moves[XG.n - 1]); xlast_to = X.XTO(XG.moves[XG.n - 1]); }
  status = '';
  xq_refresh_legal();
  turn_start = now();
  clock_update_live();
  save_auto();
}

const score_text = d => X.IS_MATE(d.score) ? `d${d.depth} ${d.san} M${d.score < 0 ? '-' : ''}${Math.floor((X.MATE - Math.abs(d.score) + 1) / 2)}`
                                           : `d${d.depth} ${d.san} ${d.score >= 0 ? '+' : ''}${d.score}`;
async function xq_cpu_move() {
  const L = LEVELS[cfg.level];
  let m;
  if (cfg.level === 0) { await sleep(250); m = XG.legal[Math.floor(Math.random() * XG.legal.length)]; }
  else {
    thinking = 1; status = 'Thinking...'; redraw();
    const n0 = XG.n;
    const r = await run_search(L.depth, think_time(L.ms), L.noise, d => { status = score_text(d); redraw(); });
    thinking = 0;
    if (r.discard || XG.n !== n0 || XG.over || !xq) return;
    last = { depth: r.depth, nodes: r.nodes, ms: r.ms, score: r.score };
    m = r.best || XG.legal[0];
    xcpu_score = r.score;
  }
  xq_do_move(m);
}
async function xq_hint() {
  if (XG.over || !XG.legal.length || thinking) return;
  thinking = 1; status = 'Thinking...'; redraw();
  const n0 = XG.n;
  const r = await run_search(40, 1500, 0);
  thinking = 0;
  if (r.discard || XG.n !== n0 || !r.best || !xq) return;
  xhint_from = X.XFROM(r.best); xhint_to = X.XTO(r.best);
  status = 'Hint: ' + X.xq_move_to_wxf(XCUR(), r.best);
}

function xq_move_cursor(dx, dy) {
  if (cfg.flip) { dx = -dx; dy = -dy; }
  xcur = X.XSQ((X.XFILE(xcur) + dx + 9) % 9, (X.XRANK(xcur) + dy + 10) % 10);
}
function xq_select() {
  if (XG.over || !is_human(XCUR().side) || thinking) return;
  const pc = XCUR().b[xcur];
  const mine = pc && X.XCOLOR(pc) === XCUR().side, has = XG.legal.some(m => X.XFROM(m) === xcur);
  if (xsel < 0) { if (mine && has) xsel = xcur; else beep(220, 40); return; }
  if (xcur === xsel) { xsel = -1; return; }
  const m = XG.legal.find(m => X.XFROM(m) === xsel && X.XTO(m) === xcur);
  if (m) { xq_do_move(m); return; }
  if (mine && has) xsel = xcur; else beep(220, 40);
}

/* RESIGN (twice; Shift+Backspace at once) and DRAW, as chess's */
function xq_resign(now_) {
  if (XG.over) { status = 'Game over'; return; }
  if (cfg.mode === MODE_CC) { status = 'Demo: NEW or MODE'; return; }
  if (!now_ && confirm !== 'R') { confirm = 'R'; status = 'Resign? RESIGN again'; return; }
  confirm = '';
  if (thinking) move_now_discard();
  xresigner = cfg.mode === MODE_HC ? cfg.human : XCUR().side;
  XG.over = 7; clk_live = 0; beep(330, 300); save_auto();
}
function xq_draw_offer() {
  if (XG.over) { status = 'Game over'; return; }
  if (cfg.mode === MODE_CC) { status = 'Demo: no draw'; return; }
  if (cfg.mode === MODE_HH) {
    if (offer === (XCUR().side ^ 1)) {
      if (confirm !== 'A') { confirm = 'A'; status = 'Draw? ACCEPT again'; return; }
      confirm = ''; offer = -1; XG.over = 8; clk_live = 0; beep(660, 250); save_auto(); return;
    }
    if (offer === XCUR().side) { status = 'Offered: now move'; return; }
    offer = XCUR().side; status = (offer ? 'Black' : 'Red') + ' offers a draw';
    return;
  }
  /* the CPU accepts when it is worse (its last score -30 or less), or from move 20 when it is not better */
  if (!(xcpu_score <= -30 || (xcpu_score <= 0 && XG.n >= 40))) { status = 'CPU declines the draw'; beep(220, 80); return; }
  if (thinking) move_now_discard();
  XG.over = 8; clk_live = 0; beep(660, 250); save_auto();
}

function xq_softkey(fn) {
  const k = cfg.page * 6 + fn;
  if (!((k === 9 && confirm === 'R') || (k === 10 && confirm === 'A'))) confirm = '';
  if (fn === 5) { cfg.page = (cfg.page + 1) % 3; store.set('cfg', cfg); return; }
  if (thinking && !(k === 0 || k === 9 || k === 10 || k === 6 || k === 12)) { status = 'Thinking... (Esc: move now)'; return; }
  switch (k) {
  case 0: if (thinking) move_now_discard(); xq_new_game(); break;
  case 1: cfg.level = (cfg.level + 1) % 10; status = 'Level ' + LEVELS[cfg.level].name; break;
  case 2: cfg.flip ^= 1; break;
  case 3: xq_undo(); break;
  case 4: xq_hint(); break;
  case 6: piece_style(); break;
  case 7: cfg.tc = (cfg.tc + 1) % TCS.length; clock_reset(); clock_update_live(); status = 'Clock ' + TCS[cfg.tc].name; break;
  case 8: cfg.human ^= 1; cfg.flip = cfg.human; if (cfg.mode !== MODE_HC) cfg.mode = MODE_HC; xsel = -1;
          status = 'You play ' + (cfg.human ? 'Black' : 'Red'); break;
  case 9: xq_resign(0); break;
  case 10: xq_draw_offer(); break;
  case 12: game_switch(); break;
  case 13: cfg.mode = (cfg.mode + 1) % MODE_CNT; xsel = -1; status = MODE_NAME[cfg.mode]; break;
  case 14: status = copy(X.xq_to_fen(XCUR())) ? 'FEN copied' : 'FEN: see the console'; console.log(X.xq_to_fen(XCUR())); break;
  }
  store.set('cfg', cfg);
}
function xq_key(k) {
  status = thinking ? status : '';
  switch (k) {
  case 'ArrowUp': case '8': xq_move_cursor(0, 1); break;
  case 'ArrowDown': case '2': xq_move_cursor(0, -1); break;
  case 'ArrowLeft': case '4': xq_move_cursor(-1, 0); break;
  case 'ArrowRight': case '6': xq_move_cursor(1, 0); break;
  case '7': xq_move_cursor(-1, 1); break;
  case '9': xq_move_cursor(1, 1); break;
  case '1': xq_move_cursor(-1, -1); break;
  case '3': xq_move_cursor(1, -1); break;
  case '5': case 'Enter': case ' ': xq_select(); break;
  case 'Escape': if (thinking) move_now(); xsel = xhint_from = xhint_to = -1; break;
  case 'Backspace': xq_undo(); break;
  }
}

function xq_serialize() {
  if (!XG.hist.length) return null;
  return { start: XG.start, moves: XG.moves.slice(0, XG.n).map(m => X.xq_move_to_str(m, 1)),
           over: XG.over === 7 || XG.over === 8 ? XG.over : 0, resigner: xresigner };
}
function xq_deserialize(s) {
  if (!s || !s.start || !xq_new_game_from(s.start)) return 0;
  for (const u of s.moves) { const m = X.xq_str_to_move(XCUR(), u); if (!m) break; xq_do_move(m); }
  if (s.over) { XG.over = s.over; xresigner = s.resigner ?? -1; clk_live = 0; }
  status = '';
  return 1;
}

/* ---- the games' menu (SHIFT) ---- */
function game_switch() {                                  /* chess <-> Xiangqi, each keeps its game */
  if (thinking) move_now_discard();
  xq ^= 1; cfg.game = xq ? 'xq' : 'chess';
  offer = -1; confirm = '';
  if (xq && !XG.hist.length && !xq_deserialize(store.get('xq'))) xq_new_game_from(XQ_START);
  if (!xq) refresh_legal();
  clock_reset(); clock_update_live();
  worker_call({ cmd: 'newgame' });
  status = xq ? 'Xiangqi' : 'Chess';
  save_auto();
}
function piece_style() {
  if (xq) { cfg.xstyle = (cfg.xstyle + 1) % 3; status = XQ_STYLE[cfg.xstyle]; }
  else { cfg.pstyle ^= 1; status = cfg.pstyle ? 'Pieces: letters' : 'Pieces: figures'; }
  store.set('cfg', cfg);
}
function zoom_toggle() { cfg.zoom ^= 1; status = cfg.zoom ? 'Zoom: +/- back' : 'Normal view'; store.set('cfg', cfg); }
function game_menu(fn) {
  shift = 0;
  if (!(fn === 3 && confirm === 'R') && !(fn === 4 && confirm === 'A')) confirm = '';
  switch (fn) {
  case 0: game_switch(); break;
  case 1: piece_style(); break;
  case 2: zoom_toggle(); break;
  case 3: if (xq) xq_resign(0); else resign(0); break;
  case 4: if (xq) xq_draw_offer(); else draw(); break;
  }
}

/* ---- Xiangqi's drawing ---- */
const xpx = f => XB0 + (cfg.flip ? 8 - f : f) * XGAP;
const xpy = r => XY0 + (cfg.flip ? r : 9 - r) * XGAP;
function xline(x0, y0, x1, y1) {                          /* horizontal, vertical or 45 degrees */
  const dx = Math.sign(x1 - x0), dy = Math.sign(y1 - y0);
  if (!dy) { fill(Math.min(x0, x1), y0, Math.abs(x1 - x0) + 1, 1, 1); return; }
  if (!dx) { fill(x0, Math.min(y0, y1), 1, Math.abs(y1 - y0) + 1, 1); return; }
  for (let x = x0, y = y0; ; x += dx, y += dy) { fill(x, y, 1, 1, 1); if (x === x1) break; }
}
function xq_grid() {
  for (let r = 0; r < 10; r++) xline(xpx(0), xpy(r), xpx(8), xpy(r));
  for (let f = 0; f < 9; f++) {
    if (f === 0 || f === 8) xline(xpx(f), xpy(0), xpx(f), xpy(9));
    else { xline(xpx(f), xpy(0), xpx(f), xpy(4)); xline(xpx(f), xpy(5), xpx(f), xpy(9)); }
  }
  for (const r0 of [0, 7]) { xline(xpx(3), xpy(r0), xpx(5), xpy(r0 + 2)); xline(xpx(5), xpy(r0), xpx(3), xpy(r0 + 2)); }
  const ym = ((xpy(4) + xpy(5)) >> 1) - 7, step = cfg.zoom ? 22 : 19;   /* the river: 楚河 漢界 */
  for (let k = 0; k < 4; k++) {
    const x0 = XB0 + (k < 2 ? 2 : 6) * XGAP - ((step + 15) >> 1) + (k & 1) * step;
    for (let y = 0; y < 15; y++) for (let x = 0; x < 15; x++) if (XB.river[k][y] >> (15 - x) & 1) fill(x0 + x, ym + y, 1, 1, 1);
  }
}
function xq_piece(pc, cx, cy, inv) {
  const n = XPS, bm = (cfg.zoom ? XB.pieces : XB.piecesN)[cfg.xstyle][X.XCOLOR(pc)][X.XTYPE(pc) - 1];
  const disc = cfg.zoom ? XB.disc : XB.discN, x0 = cx - (n >> 1), y0 = cy - (n >> 1);
  for (let y = 0; y < n; y++)
    for (let x = 0; x < n; x++)
      if (disc[y] >> (23 - x) & 1) fill(x0 + x, y0 + y, 1, 1, (bm[y] >> (23 - x) & 1) ^ inv);
}
function xq_frame(cx, cy, r) {
  fill(cx - r, cy - r, 2 * r + 1, 1, 1); fill(cx - r, cy + r, 2 * r + 1, 1, 1);
  fill(cx - r, cy - r, 1, 2 * r + 1, 1); fill(cx + r, cy - r, 1, 2 * r + 1, 1);
}
/* four corners: they stay in the gap between the pieces, so nothing next to it is drawn over */
function xq_corners(cx, cy, r, len, w) {
  for (const sx of [-1, 1]) for (const sy of [-1, 1]) {
    const x = sx < 0 ? cx - r : cx + r - w + 1, y = sy < 0 ? cy - r : cy + r - w + 1;
    fill(sx < 0 ? x : cx + r - len + 1, y, len, w, 1);
    fill(x, sy < 0 ? y : cy + r - len + 1, w, len, 1);
  }
}
function xq_draw_board() {
  xq_grid();
  const tg = new Set(xsel >= 0 ? XG.legal.filter(m => X.XFROM(m) === xsel).map(m => X.XTO(m)) : []);
  for (let r = 0; r < 10; r++)
    for (let f = 0; f < 9; f++) {
      const s = X.XSQ(f, r), pc = XCUR().b[s], cx = xpx(f), cy = xpy(r);
      if (pc) xq_piece(pc, cx, cy, s === xsel ? 1 : 0);
      if (tg.has(s)) { if (pc) xq_frame(cx, cy, XR); else fill(cx - 2, cy - 2, 5, 5, 1); }
      if (s === xlast_from || s === xlast_to || s === xhint_from || s === xhint_to) xq_corners(cx, cy, XR, 4, 1);
      if (s === xcur && !thinking && is_human(XCUR().side) && !XG.over) xq_corners(cx, cy, XR, cfg.zoom ? 7 : 6, 2);
    }
}
function xq_draw_panel() {
  const h = LH;
  if (!cfg.tc) ptext(2, 1, 'XIANGQI 47', 1);
  else {
    const stm = XCUR().side;
    ptext(0, 1, ' R ' + fmt_clock(clock_left(0)) + ' ', stm === 0 && !XG.over);
    ptext((XPANEL >> 1) + 2, 1, ' B ' + fmt_clock(clock_left(1)) + ' ', stm === 1 && !XG.over);
    fill(XPANEL >> 1, 0, 1, h + 2, 1);
  }
  let y = h + 4;
  const lv = cfg.level ? 'L' + LEVELS[cfg.level].name : LEVELS[cfg.level].name;
  ptext(2, y, cfg.mode === MODE_HH ? '2 players' : cfg.mode === MODE_CC ? 'Demo  ' + lv : `${lv}  You:${cfg.human ? 'Black' : 'Red'}`, 0);
  y += h;
  let s, inv = 0;
  if (XG.over) { s = xq_result(XG.over); inv = 1; }
  else if (X.xq_in_check(XCUR())) { s = 'CHECK! ' + (XCUR().side ? 'Black' : 'Red'); inv = 1; }
  else if (status) s = status;
  else if (cfg.mode === MODE_HH && offer === (XCUR().side ^ 1)) s = (offer ? 'Black' : 'Red') + ' offers a draw';
  else s = (XCUR().side ? 'Black' : 'Red') + ' to move';
  ptext(2, y, s, inv);
  y += h + 2;
  fill(2, y, XPANEL - 4, 1, 1);
  y += 3;
  if (xseven) {                                           /* the Seven Stars: its name, 七星聚會 */
    for (let k = 0; k < 4; k++) for (let yy = 0; yy < 15; yy++) for (let x = 0; x < 15; x++)
      if (XB.seven[k][yy] >> (15 - x) & 1) fill(4 + k * 18 + x, y + 1 + yy, 1, 1, 1);
    y += 18;
  }
  const bottom = cfg.zoom ? LCD_Y - 2 : MENU_Y - 2, lines = Math.max(1, Math.floor((bottom - y) / h));
  const total = (XG.n + 1) >> 1, first = Math.max(0, total - lines);
  for (let ln = first; ln < total; ln++) {
    ptext(2, y, (ln + 1) + '.', 0); ptext(46, y, XG.wxf[2 * ln], 0);
    if (2 * ln + 1 < XG.n) ptext(114, y, XG.wxf[2 * ln + 1], 0);
    y += h;
  }
}

/* ---- drawing: the pixel layer at 400 x 240, then the text at S times ---- */
const canvas = document.getElementById('lcd');
canvas.width = LCD_X * S; canvas.height = LCD_Y * S;
const ctx = canvas.getContext('2d');
const px = document.createElement('canvas'); px.width = LCD_X; px.height = LCD_Y;
const pctx = px.getContext('2d');
const img = pctx.createImageData(LCD_X, LCD_Y);
const lcd = new Uint8Array(LCD_X * LCD_Y);
const INK_RGB = [0x22, 0x26, 0x1e], PAPER_RGB = [0xc7, 0xcd, 0xba];
function fill(x, y, w, h, v) {
  for (let j = Math.max(0, y); j < Math.min(LCD_Y, y + h); j++) lcd.fill(v, j * LCD_X + Math.max(0, x), j * LCD_X + Math.min(LCD_X, x + w));
}
function bmp_at(b, w, h, x, y) { if (x < 0 || y < 0 || x >= w || y >= h) return 0; const c = b[y][x]; return c === '#' ? 1 : c === '+' ? 2 : 0; }
/* a piece's pixel at (x, y) of its 20 px glyph: -1 transparent, 0 white, 1 black (src/main.c's piece_px) */
function piece_px(pc, x, y) {
  const b = cfg.pstyle ? BMP.letters[PTYPE(pc)] : BMP.pieces[PTYPE(pc)], W = 20, H = 20;
  const v = bmp_at(b, W, H, x, y);
  if (cfg.pstyle) {                                       /* letters: White solid (a white halo), Black white in a black box */
    if (PCOLOR(pc) === BLACK) return x >= 2 && x <= 17 && y >= 3 && y <= 18 ? (v ? 0 : 1) : -1;
    if (v) return 1;
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) if (bmp_at(b, W, H, x + dx, y + dy)) return 0;
    return -1;
  }
  if (!v) {
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) if (bmp_at(b, W, H, x + dx, y + dy)) return 0;
    return -1;
  }
  if (PCOLOR(pc) === BLACK) return v === 2 ? 0 : 1;
  if (v === 2) return 1;
  if (!bmp_at(b, W, H, x - 1, y) || !bmp_at(b, W, H, x + 1, y) || !bmp_at(b, W, H, x, y - 1) || !bmp_at(b, W, H, x, y + 1)) return 1;
  return 0;
}
const sq_x = sq => BX + (cfg.flip ? 7 - FILE_OF(sq) : FILE_OF(sq)) * SQPX;
const sq_y = sq => BY + (cfg.flip ? RANK_OF(sq) : 7 - RANK_OF(sq)) * SQPX;
function is_target(sq) {
  if (sel === NOSQ) return 0;
  geometry();
  for (const m of G.legal) {
    if (MFROM(m) !== sel) continue;
    if (MTO(m) === sq && !(MFLAGS(m) & F_CASTLE)) return 1;
    if ((MFLAGS(m) & F_CASTLE) && E.castle_rook_sq(CUR(), m) === sq) return 1;
  }
  return 0;
}
function draw_square(sq) {
  const p = CUR(), x0 = sq_x(sq), y0 = sq_y(sq);
  const dark = !((FILE_OF(sq) + RANK_OF(sq)) & 1), pc = p.b[sq], target = is_target(sq);
  const g = GLYPH, ox = (SQPX - g) >> 1, oy = ((SQPX - g) >> 1) + 1, Ed = SQPX - 1, C = SQPX >> 1;
  for (let y = 0; y < SQPX; y++) for (let x = 0; x < SQPX; x++) {
    let v = dark ? (((x + y) & 1) === 0 && (y & 1) === 0 ? 1 : 0) : 0;
    if (pc) {                                             /* the glyph scaled to g px (nearest pixel) */
      const gx = x - ox < 0 ? -1 : Math.floor((x - ox) * 20 / g), gy = y - oy < 0 ? -1 : Math.floor((y - oy) * 20 / g);
      const q = piece_px(pc, gx, gy); if (q >= 0) v = q;
    }
    if (target) {
      if (!pc && x >= C - 2 && x <= C + 1 && y >= C - 2 && y <= C + 1) v = 1;
      if (pc && ((x < 4 && y < 4 && x + y < 4) || (x > Ed - 4 && y < 4 && (Ed - x) + y < 4) ||
                 (x < 4 && y > Ed - 4 && x + (Ed - y) < 4) || (x > Ed - 4 && y > Ed - 4 && (Ed - x) + (Ed - y) < 4))) v = 1;
    }
    if ((sq === last_from || sq === last_to) && (x === 0 || y === 0 || x === SQPX - 1 || y === SQPX - 1)) v = 1;
    if ((sq === hint_from || sq === hint_to) && (x <= 1 || y <= 1 || x >= SQPX - 2 || y >= SQPX - 2) && ((x + y) & 2)) v = 1;
    if (sq === sel && (x < 3 || x >= SQPX - 3 || y < 3 || y >= SQPX - 3)) v = 1;
    if (sq === cur && !thinking && is_human(p.side) && !G.over) {
      const edge = x < 2 || x >= SQPX - 2 || y < 2 || y >= SQPX - 2;
      const inner = (x === 2 || x === SQPX - 3 || y === 2 || y === SQPX - 3);
      if (edge) v = 1; else if (inner) v = 0;
    }
    lcd[(y0 + y) * LCD_X + x0 + x] = v;
  }
}
const texts = [];                                          /* [x, y, text, inverse, centred width] at 400 x 240 */
function ptext(x, y, s, inv) {
  ctx.font = FONT;
  while (s && x + ctx.measureText(s).width / S > PANEL_EDGE) s = s.slice(0, -1);
  if (inv && s) fill(Math.max(0, x - 2), y, Math.ceil(ctx.measureText(s).width / S) + 4, LH, 1);
  texts.push([x, y, s, inv]);
}
const material = () => { const v = [0, 1, 3, 3, 5, 9, 0]; let s = 0; const b = CUR().b;
  for (let q = 0; q < 128; q++) if (ONBOARD(q) && b[q]) s += PCOLOR(b[q]) ? -v[PTYPE(b[q])] : v[PTYPE(b[q])]; return s; };

function draw_panel() {
  const h = LH;
  if (!cfg.tc) {
    ptext(2, 1, 'CHESS 47', 1);
    const s = G.pos960 >= 0 ? '960 #' + G.pos960 : G.c960 ? 'FEN' : 'web';
    ctx.font = FONT; ptext(PANEL_W - Math.ceil(ctx.measureText(s).width / S), 1, s, 0);
  } else {
    const stm = CUR().side;
    ptext(0, 1, ' W ' + fmt_clock(clock_left(WHITE)) + ' ', stm === WHITE && !G.over);
    ptext(PANEL_W / 2 + 2, 1, ' B ' + fmt_clock(clock_left(BLACK)) + ' ', stm === BLACK && !G.over);
    fill(PANEL_W / 2, 0, 1, h + 2, 1);
  }
  let y = h + 4;
  const lv = cfg.level ? 'L' + LEVELS[cfg.level].name : LEVELS[cfg.level].name;
  ptext(2, y, cfg.mode === MODE_HH ? '2 players' : cfg.mode === MODE_CC ? 'Demo  ' + lv : `${lv}  You:${cfg.human ? 'Black' : 'White'}`, 0);
  y += h;
  geometry();
  let s, inv = 0;
  if (G.over) { s = result_text(G.over, CUR().side); inv = 1; }
  else if (promo_mode) s = 'Promote to?';
  else if (E.in_check(CUR())) { s = `CHECK! ${CUR().side ? 'Black' : 'White'} to move`; inv = 1; }
  else if (status) s = status;
  else if (accept_pending()) s = (offer ? 'Black' : 'White') + ' offers a draw';
  else s = `${CUR().side ? 'Black' : 'White'} to move`;
  ptext(2, y, s, inv);
  y += h + 2;
  fill(2, y, PANEL_W - 4, 1, 1);
  y += 3;
  const crazy = cfg.level === 0 && cfg.mode !== MODE_HH;
  const bottom = crazy ? BOARD_PX - 46 : BOARD_PX - h;
  const lines = Math.max(1, Math.floor((bottom - y) / h));
  const h0 = G.hist[0], total = Math.floor((G.n + 1 + (h0.side === BLACK ? 1 : 0)) / 2);
  const first = Math.max(0, total - lines);
  for (let ln = first; ln < total; ln++) {
    const iw = ln * 2 - (h0.side === BLACK ? 1 : 0);
    const w = iw >= 0 && iw < G.n ? G.san[iw] : iw < 0 ? '...' : '';
    const b = iw + 1 >= 0 && iw + 1 < G.n ? G.san[iw + 1] : '';
    ptext(2, y, (h0.fullmove + ln) + '.', 0); ptext(40, y, w, 0); ptext(112, y, b, 0);
    y += h;
  }
  if (crazy) {
    const c = BMP.crazy;
    for (let j = 0; j < c.length; j++) for (let i = 0; i < c[j].length; i++) if (c[j][i] === '#') fill(4 + i * 2, BOARD_PX - 46 + j * 2, 2, 2, 1);
    ptext(60, BOARD_PX - 36, 'CRAZY!', 1);
  } else {
    const m = material();
    ptext(2, BOARD_PX - h, `${E.sqname(cur)}  ${m > 0 ? '+' : ''}${m}`, 0);
  }
}
const menu_boxes = [];
/* SHIFT: the game menu, the same in both games: the other game, the pieces' style, the zoom, RESIGN, DRAW */
function game_menu_keys() {
  const acc = cfg.mode === MODE_HH && offer === (side_now() ^ 1) && !over_now();
  return [xq ? 'CHESS' : 'XIANGQI', 'PIECES', cfg.zoom ? 'NORMAL' : 'ZOOM', 'RESIGN', acc ? 'ACCEPT' : 'DRAW', ''];
}
function draw_menu() {
  menu_boxes.length = 0;
  if (cfg.zoom && !shift && !promo_mode) return;          /* zoom: the board the full height (the keys still work) */
  const keys = shift && !promo_mode ? game_menu_keys() : xq ? XQ_MENU[cfg.page] : promo_mode ? PROMO_MENU : MENU[cfg.page].map((k, i) => cfg.page === 1 && i === 0 && G.c960 ? '960*' :
    accept_pending() && ((cfg.page === 0 && i === 4) || (cfg.page === 1 && i === 4)) ? 'ACCEPT' : k);
  const w = 64, gap = (LCD_X - 6 * w) / 5;
  for (let i = 0; i < 6; i++) {
    const x = Math.round(i * (w + gap));
    menu_boxes.push([x, MENU_Y, w, MENU_H]);
    if (!keys[i]) continue;
    fill(x, MENU_Y, w, MENU_H, 1);
    texts.push([x, MENU_Y + 2, keys[i], 1, w]);
  }
}
function draw_info() {
  const t = [
    'CHESS 47 web',
    'The DM42n engine in JavaScript',
    `Level ${LEVELS[cfg.level].name}: depth ${LEVELS[cfg.level].depth}` + (LEVELS[cfg.level].ms ? `, ${LEVELS[cfg.level].ms / 1000} s` : ''),
    cfg.calc ? `DM42n speed: ${DM42N_NPS}k nodes/s` : 'Full speed of this computer',
    `Last search: depth ${last.depth}, ${last.nodes} nodes`,
    last.ms ? `${Math.round(last.nodes / Math.max(1, last.ms))}k nodes/s here` : '',
    'Hash table: 16 MB',
    'GPL-3.0-or-later (c) 2026 Victor Valdes',
    'Any key: back',
  ];
  t.forEach((s, i) => s && texts.push([12, 6 + i * 23, s, i === 0 ? 1 : 0]));
  if (t[0]) fill(10, 6, 150, LH, 1);
}

function redraw() {
  layout();
  lcd.fill(0); texts.length = 0;
  if (info_screen) draw_info();
  else if (xq) { xq_draw_board(); xq_draw_panel(); }
  else {
    for (let r = 0; r < 8; r++) for (let f = 0; f < 8; f++) draw_square(SQ(f, r));
    fill(BX - 1, BY, 1, BOARD_PX, 1);
    draw_panel();
  }
  draw_menu();
  for (let i = 0; i < lcd.length; i++) {
    const c = lcd[i] ? INK_RGB : PAPER_RGB;
    img.data[i * 4] = c[0]; img.data[i * 4 + 1] = c[1]; img.data[i * 4 + 2] = c[2]; img.data[i * 4 + 3] = 255;
  }
  pctx.putImageData(img, 0, 0);
  ctx.imageSmoothingEnabled = false;
  ctx.drawImage(px, 0, 0, LCD_X * S, LCD_Y * S);
  ctx.font = FONT; ctx.textBaseline = 'top';
  for (const [x, y, s, inv, cw] of texts) {
    ctx.fillStyle = inv ? PAPER : INK;
    ctx.font = cw && ctx.measureText(s).width > (cw - 4) * S ? FONT.replace(`${16 * S}px`, `${13 * S}px`) : FONT;   /* XIANGQI */
    const tx = cw ? (x + cw / 2) * S - ctx.measureText(s).width / 2 : x * S;
    ctx.fillText(s, tx, (y + 2) * S);
  }
}

/* ---- input ---- */
document.addEventListener('keydown', ev => {
  if (document.querySelector('dialog[open]')) return;
  if (ev.key === 'Shift') { shift_alone = 1; return; }
  shift_alone = 0;
  const fk = /^F([1-6])$/.exec(ev.key);
  if (ev.code === 'Digit7' && ev.shiftKey) { ev.preventDefault(); xq_seven(); }
  else if (fk) { ev.preventDefault(); if (ev.shiftKey) shift = 1; softkey(+fk[1] - 1); }
  else if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Enter', ' ', 'Escape', 'Backspace',
            '1', '2', '3', '4', '5', '6', '7', '8', '9', 'i', '*', 'x', '+', '-'].includes(ev.key)) { ev.preventDefault(); key(ev.key, ev.shiftKey); }
  else return;
  redraw(); tick_game();
});
/* Shift pressed and let go alone: the calculator's SHIFT, the game menu on the soft keys (again: off) */
let shift_alone = 0;
document.addEventListener('keyup', ev => {
  if (ev.key !== 'Shift' || !shift_alone || document.querySelector('dialog[open]')) return;
  shift_alone = 0; shift ^= 1; redraw();
});
document.getElementById('shift').addEventListener('click', () => { shift ^= 1; redraw(); });
canvas.addEventListener('pointerdown', ev => {
  const r = canvas.getBoundingClientRect();
  const x = (ev.clientX - r.left) * LCD_X / r.width, y = (ev.clientY - r.top) * LCD_Y / r.height;
  layout();
  if (info_screen) { info_screen = 0; }
  else if (y >= MENU_Y && menu_boxes.length) { menu_boxes.forEach((b, i) => { if (x >= b[0] && x < b[0] + b[2]) softkey(i); }); }
  else if (xq) {
    if (x >= XPANEL + 2) {                                /* the nearest point */
      let f = Math.round((x - XB0) / XGAP), rk = 9 - Math.round((y - XY0) / XGAP);
      f = Math.max(0, Math.min(8, f)); rk = Math.max(0, Math.min(9, rk));
      if (cfg.flip) { f = 8 - f; rk = 9 - rk; }
      xcur = X.XSQ(f, rk);
      shift = 0;
      xq_select();
    }
  }
  else if (x >= BX && y < BOARD_PX) {
    let f = Math.floor((x - BX) / SQPX), rk = 7 - Math.floor(y / SQPX);
    if (cfg.flip) { f = 7 - f; rk = 7 - rk; }
    cur = SQ(f, rk);
    if (!promo_mode) select_square();
  }
  redraw(); tick_game();
});
document.getElementById('calc').checked = !!cfg.calc;
document.getElementById('sound').checked = !!cfg.sound;
document.getElementById('sound').addEventListener('change', ev => { cfg.sound = ev.target.checked ? 1 : 0; store.set('cfg', cfg); });
document.getElementById('calc').addEventListener('change', ev => { cfg.calc = ev.target.checked ? 1 : 0; store.set('cfg', cfg); });

/* ---- the game loop: the CPU's moves, the demo, the clock ---- */
let busy = 0;
async function tick_game() {
  if (xq) {
    if (busy || thinking || XG.over || info_screen || !XG.legal.length || is_human(XCUR().side)) return;
    busy = 1;
    try { await xq_cpu_move(); } finally { busy = 0; }
    redraw();
    if (cfg.mode === MODE_CC) setTimeout(tick_game, 400); else tick_game();
    return;
  }
  if (busy || thinking || G.over || promo_mode || info_screen) return;
  if (is_human(CUR().side) || !G.legal.length) return;
  busy = 1;
  try { await cpu_move(); } finally { busy = 0; }
  redraw();
  if (cfg.mode === MODE_CC) setTimeout(tick_game, 400); else tick_game();
}
setInterval(() => {
  if (clk_live && !over_now()) {
    const sd = side_now();
    if (clock_left(sd) <= 0) { clk[sd] = 0; if (xq) XG.over = 6; else G.over = 6; clk_live = 0; beep(330, 400); if (thinking) move_now(); }
    redraw();
  }
}, 250);

/* ---- start: the last game resumed ---- */
const start_xq = xq;
xq = 0;                                                   /* chess's game first (it is always there) */
if (!deserialize(store.get('auto'))) new_game(-1); else status = G.n ? 'Game resumed' : '';
if (start_xq) { xq = 1; if (!xq_deserialize(store.get('xq'))) xq_new_game_from(XQ_START); else status = XG.n ? 'Game resumed' : ''; }
redraw();
tick_game();
})();
