/*
 * CHESS47 web - the DM42n program's screen and keys in the browser (src/main.c), the engine (engine.js) in a
 * Web Worker. (C) 2026 Victor Valdes - GPL-3.0-or-later
 */
'use strict';
(function () {
const E = window.Chess47;
const { WHITE, BLACK, NOSQ, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, F_CAP, F_EP, F_CASTLE } = E;
const { MFROM, MTO, MPROMO, MFLAGS, PTYPE, PCOLOR, SQ, FILE_OF, RANK_OF, ONBOARD } = E;

/* ---- layout: the DM42n's 400 x 240 LCD, drawn S times larger ---- */
const LCD_X = 400, LCD_Y = 240, S = 3;
const SQPX = 26, BX = 192, BY = 0, BOARD_PX = 8 * SQPX, PANEL_W = 186;
const MENU_Y = 216, MENU_H = 24;
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
const cfg = Object.assign({ level: 4, mode: MODE_HC, human: WHITE, flip: 0, tc: 0, sound: 1, page: 0, calc: 1 }, store.get('cfg') || {});
const G = { hist: [E.newPos()], moves: [], san: [], keys: [], n: 0, over: 0, pos960: -1, legal: [], start: null, rfile: [7, 0], c960: 0 };
let cur = 0x14, sel = NOSQ, hint_from = NOSQ, hint_to = NOSQ, last_from = NOSQ, last_to = NOSQ;
let status = '', promo_mode = 0, promo_opts = [0, 0, 0, 0], thinking = 0, kirk = 0, info_screen = 0;
let resigner = -1, offer = -1, confirm = '', cpu_score = 0, cpu_scored = 0;   /* RESIGN, DRAW, the CPU's view */
let clk = [0, 0], turn_start = 0, clk_live = 0;
let last = { depth: 0, nodes: 0, ms: 0, score: 0 };
const CUR = () => G.hist[G.n];
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
function clock_left(side) { return clk_live && side === CUR().side ? clk[side] - (now() - turn_start) : clk[side]; }
function clock_update_live() { clk_live = cfg.tc && !G.over && G.n > 0 ? 1 : 0; if (clk_live) turn_start = turn_start || now(); }
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
  const E = self.Chess47;
  E.eng_init(1 << 20);
  self.onmessage = ev => {
    const d = ev.data;
    if (d.cmd === 'newgame') { E.eng_clear_tt(); return; }
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
    worker_call({ cmd: 'search', start: G.start, rfile: G.rfile, c960: G.c960, moves: G.moves.slice(0, G.n),
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
  const left = clock_left(CUR().side);
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
function save_auto() { store.set('auto', serialize()); store.set('cfg', cfg); }

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

function key(k, shift) {
  if (info_screen) { info_screen = 0; return; }
  if (shift && k === 'Backspace') { resign(1); return; }
  if (k === 'i') { info_screen = 1; return; }
  if (promo_mode) { if (k === 'Escape') { promo_mode = 0; sel = NOSQ; } return; }
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
function piece_px(pc, x, y) {
  const b = BMP.pieces[PTYPE(pc)], W = 20, H = 20;
  const v = bmp_at(b, W, H, x, y);
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
  const ox = (SQPX - 20) >> 1, oy = ((SQPX - 20) >> 1) + 1;
  for (let y = 0; y < SQPX; y++) for (let x = 0; x < SQPX; x++) {
    let v = dark ? (((x + y) & 1) === 0 && (y & 1) === 0 ? 1 : 0) : 0;
    if (pc) { const q = piece_px(pc, x - ox, y - oy); if (q >= 0) v = q; }
    if (target) {
      if (!pc && x >= 11 && x <= 14 && y >= 11 && y <= 14) v = 1;
      if (pc && ((x < 4 && y < 4 && x + y < 4) || (x > 21 && y < 4 && (25 - x) + y < 4) ||
                 (x < 4 && y > 21 && x + (25 - y) < 4) || (x > 21 && y > 21 && (25 - x) + (25 - y) < 4))) v = 1;
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
  while (s && x + ctx.measureText(s).width / S > BX - 2) s = s.slice(0, -1);
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
function draw_menu() {
  const keys = promo_mode ? PROMO_MENU : MENU[cfg.page].map((k, i) => cfg.page === 1 && i === 0 && G.c960 ? '960*' :
    accept_pending() && ((cfg.page === 0 && i === 4) || (cfg.page === 1 && i === 4)) ? 'ACCEPT' : k);
  menu_boxes.length = 0;
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
  lcd.fill(0); texts.length = 0;
  if (info_screen) draw_info();
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
    const tx = cw ? (x + cw / 2) * S - ctx.measureText(s).width / 2 : x * S;
    ctx.fillText(s, tx, (y + 2) * S);
  }
}

/* ---- input ---- */
document.addEventListener('keydown', ev => {
  if (document.querySelector('dialog[open]')) return;
  const fk = /^F([1-6])$/.exec(ev.key);
  if (fk) { ev.preventDefault(); softkey(+fk[1] - 1); }
  else if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Enter', ' ', 'Escape', 'Backspace',
            '1', '2', '3', '4', '5', '6', '7', '8', '9', 'i'].includes(ev.key)) { ev.preventDefault(); key(ev.key, ev.shiftKey); }
  else return;
  redraw(); tick_game();
});
canvas.addEventListener('pointerdown', ev => {
  const r = canvas.getBoundingClientRect();
  const x = (ev.clientX - r.left) * LCD_X / r.width, y = (ev.clientY - r.top) * LCD_Y / r.height;
  if (info_screen) { info_screen = 0; }
  else if (y >= MENU_Y) { menu_boxes.forEach((b, i) => { if (x >= b[0] && x < b[0] + b[2]) softkey(i); }); }
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
  if (busy || thinking || G.over || promo_mode || info_screen) return;
  if (is_human(CUR().side) || !G.legal.length) return;
  busy = 1;
  try { await cpu_move(); } finally { busy = 0; }
  redraw();
  if (cfg.mode === MODE_CC) setTimeout(tick_game, 400); else tick_game();
}
setInterval(() => {
  if (clk_live && !G.over) {
    if (clock_left(CUR().side) <= 0) { clk[CUR().side] = 0; G.over = 6; clk_live = 0; beep(330, 400); if (thinking) move_now(); }
    redraw();
  }
}, 250);

/* ---- start: the last game resumed ---- */
if (!deserialize(store.get('auto'))) new_game(-1); else status = G.n ? 'Game resumed' : '';
redraw();
tick_game();
})();
