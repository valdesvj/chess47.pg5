/*
 * CHESS47 web - the Xiangqi engine of src/xq_engine.c (and its search, src/search.inc) in JavaScript, line for line
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 *
 * A 16 x 14 board (9 files x 10 ranks inside, padded), copy-make, the same PVS alpha-beta as chess with a
 * transposition table, null move, LMR and quiescence. The 64-bit Zobrist keys are two 32-bit halves (hi, lo), made
 * by the same xorshift64 as the C, so the keys, the node counts and the moves are the C engine's
 * (test/test_web_xq.js checks perft and fixed-depth searches against test/xq_jscheck.c).
 */
'use strict';
(function (G) {
const MAXPLY = 64, MAXMOVES = 256, MAXGAME = 400;
const INF = 32000, MATE = 31000;
const IS_MATE = s => s > MATE - MAXPLY * 2 || s < -MATE + MAXPLY * 2;
const XF_CAP = 1;
const XMV = (f, t, fl) => (f | (t << 8) | (fl << 24)) >>> 0;
const XFROM = m => m & 0xff, XTO = m => (m >>> 8) & 0xff, XFLAGS = m => m >>> 24;
const XEMPTY = 0, XKING = 1, XADVISOR = 2, XELEPHANT = 3, XHORSE = 4, XCHARIOT = 5, XCANNON = 6, XPAWN = 7;
const XRED = 0, XBLACK = 1, XOFF = 0x80;
const XTYPE = p => p & 7, XCOLOR = p => (p >> 3) & 1;
const XSQ = (f, r) => ((r + 2) << 4) | (f + 3), XFILE = s => (s & 15) - 3, XRANK = s => (s >> 4) - 2;

const X = {};

/* ---- positions: b[256], side, half, fullmove, k0 k1 (the generals), hi lo (the key) ---- */
function newPos() { return { b: new Uint8Array(256), side: 0, half: 0, fullmove: 1, k0: 0, k1: 0, hi: 0, lo: 0 }; }
function copyPos(d, s) {
  d.b.set(s.b); d.side = s.side; d.half = s.half; d.fullmove = s.fullmove; d.k0 = s.k0; d.k1 = s.k1;
  d.hi = s.hi; d.lo = s.lo; return d;
}
const ksq = (p, c) => c ? p.k1 : p.k0;
function setK(p, c, s) { if (c) p.k1 = s; else p.k0 = s; }

let tt_n = 1 << 20;                                    /* entries: the C's with eng_init's 16 MB (16-byte entries) */
let ttH, ttL, ttM, ttS, ttD, ttF;
const TT_LOWER = 1, TT_UPPER = 2, TT_EXACT = 3;

const MBUF = MAXPLY * 96;
const stk = []; for (let i = 0; i < MAXPLY + 2; i++) stk.push(newPos());
const mbuf = new Uint32Array(MBUF), sbuf = new Int32Array(MBUF);
const mofs = new Uint16Array(MAXPLY + 2);
const killer = new Uint32Array(MAXPLY * 2);
const hh = new Int16Array(16 * 256);
const repH = new Int32Array(MAXGAME + MAXPLY + 8), repL = new Int32Array(MAXGAME + MAXPLY + 8);
let rep_n = 0;
const scratch = new Uint32Array(MAXMOVES);
const zpH = new Int32Array(14 * 90), zpL = new Int32Array(14 * 90);
let zsH = 0, zsL = 0;

const ORTH = [1, -1, 16, -16];
const DIAG = [17, 15, -15, -17];
const HD = [33, 31, -31, -33, 18, 14, -14, -18];           /* horse: target */
const HL = [16, 16, -16, -16, 1, -1, 1, -1];              /* its leg */

const ZI = (pc, s) => ((XTYPE(pc) - 1) + 7 * XCOLOR(pc)) * 90 + XRANK(s) * 9 + XFILE(s);

function in_palace(c, s) {
  const f = XFILE(s), r = XRANK(s);
  return f >= 3 && f <= 5 && (c === XRED ? r >= 0 && r <= 2 : r >= 7 && r <= 9);
}
function own_half(c, s) { const r = XRANK(s); return c === XRED ? r <= 4 : r >= 5; }

/* ---- init: the C's xorshift64 from 0x2545F4914F6CDD1D ---- */
let zinit = 0;
function xq_init(entries) {
  if (entries) tt_n = entries;
  ttH = new Int32Array(tt_n); ttL = new Int32Array(tt_n); ttM = new Uint32Array(tt_n);
  ttS = new Int16Array(tt_n); ttD = new Int8Array(tt_n); ttF = new Uint8Array(tt_n);
  if (!zinit) {
    let rs = 0x2545F4914F6CDD1Dn;
    const M = (1n << 64n) - 1n;
    const next = () => { let x = rs; x ^= (x << 13n) & M; x ^= x >> 7n; x ^= (x << 17n) & M; rs = x; return x; };
    for (let i = 0; i < 14 * 90; i++) { const x = next(); zpH[i] = Number(x >> 32n) | 0; zpL[i] = Number(x & 0xffffffffn) | 0; }
    const z = next(); zsH = Number(z >> 32n) | 0; zsL = Number(z & 0xffffffffn) | 0;
    zinit = 1;
  }
  xq_clear_tt();
  hh.fill(0);
}
/* as the C's eng_clear_tt(): the table and the killers; Xiangqi's history stays (xq_init clears it) */
function xq_clear_tt() { pvhH.fill(0); pvhL.fill(0); pvhM.fill(0); ttH.fill(0); ttL.fill(0); ttM.fill(0); ttS.fill(0); ttD.fill(0); ttF.fill(0); killer.fill(0); hh.fill(0); }

function compute_key(p) {
  let h = p.side ? zsH : 0, l = p.side ? zsL : 0;
  for (let r = 0; r < 10; r++)
    for (let f = 0; f < 9; f++) { const s = XSQ(f, r); if (p.b[s]) { const i = ZI(p.b[s], s); h ^= zpH[i]; l ^= zpL[i]; } }
  p.hi = h; p.lo = l;
}

/* ---- FEN ---- */
const LET = ' KABNRCP';
function piece_of(ch) {
  const black = ch >= 'a' && ch <= 'z';
  const u = ch.toUpperCase();
  const t = u === 'K' ? XKING : u === 'A' ? XADVISOR : (u === 'B' || u === 'E') ? XELEPHANT :
            (u === 'N' || u === 'H') ? XHORSE : u === 'R' ? XCHARIOT : u === 'C' ? XCANNON : u === 'P' ? XPAWN : 0;
  return t ? t | (black ? 8 : 0) : 0;
}

function xq_from_fen(p, fen) {
  p.b.fill(XOFF);
  for (let r = 0; r < 10; r++) for (let c = 0; c < 9; c++) p.b[XSQ(c, r)] = 0;
  p.k0 = p.k1 = 0;
  const parts = fen.trim().split(/\s+/);
  let r = 9, c = 0;
  for (const ch of parts[0]) {
    if (ch === '/') { r--; c = 0; continue; }
    if (ch >= '1' && ch <= '9') { c += +ch; continue; }
    const pc = piece_of(ch);
    if (!pc || r < 0 || c > 8) return 0;
    p.b[XSQ(c, r)] = pc;
    if (XTYPE(pc) === XKING) setK(p, XCOLOR(pc), XSQ(c, r));
    c++;
  }
  if (!p.k0 || !p.k1) return 0;
  p.side = parts[1] === 'b' ? XBLACK : XRED;
  p.half = 0; p.fullmove = 1;
  if (parts.length > 4 && /^\d+$/.test(parts[4])) p.half = Math.min(255, +parts[4]);
  if (parts.length > 5 && /^\d+$/.test(parts[5])) p.fullmove = +parts[5];
  compute_key(p);
  return 1;
}

function xq_to_fen(p) {
  let o = '';
  for (let r = 9; r >= 0; r--) {
    let e = 0;
    for (let c = 0; c < 9; c++) {
      const pc = p.b[XSQ(c, r)];
      if (!pc) { e++; continue; }
      if (e) { o += e; e = 0; }
      const ch = LET[XTYPE(pc)];
      o += XCOLOR(pc) ? ch.toLowerCase() : ch;
    }
    if (e) o += e;
    if (r) o += '/';
  }
  return o + ' ' + (p.side ? 'b' : 'w') + ' - - ' + p.half + ' ' + p.fullmove;
}

function xq_pos_start(p) { xq_from_fen(p, 'rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1'); }

/* ---- attacks and moves ---- */
function xq_attacked(p, sq, by) {
  const b = p.b;
  for (let k = 0; k < 4; k++) {
    const d = ORTH[k];
    let t = sq + d;
    while (!b[t]) t += d;
    if (b[t] === XOFF) continue;
    const q = b[t];
    if (XCOLOR(q) === by && (XTYPE(q) === XCHARIOT || (XTYPE(q) === XKING && (d === 16 || d === -16)))) return 1;
    t += d;
    while (!b[t]) t += d;
    if (b[t] !== XOFF && b[t] === (XCANNON | by << 3)) return 1;
  }
  const hz = XHORSE | by << 3;
  for (let k = 0; k < 8; k++) {
    const h = sq - HD[k];
    if (b[h] === hz && !b[h + HL[k]]) return 1;
  }
  const pz = XPAWN | by << 3, fw = by === XRED ? 16 : -16;
  if (b[sq - fw] === pz) return 1;
  if (b[sq - 1] === pz && !own_half(by, sq - 1)) return 1;
  if (b[sq + 1] === pz && !own_half(by, sq + 1)) return 1;
  return 0;
}
const xq_in_check = p => xq_attacked(p, ksq(p, p.side), p.side ^ 1);

/* ml: a Uint32Array, from index base; returns the count */
function xq_gen_moves(p, ml, caps_only, base) {
  const b = p.b, c = p.side;
  base = base || 0;
  let n = base;
  const add = (f, t) => {
    const q = b[t];
    if (q === XOFF) return;
    if (q) { if (XCOLOR(q) !== c) ml[n++] = XMV(f, t, XF_CAP); }
    else if (!caps_only) ml[n++] = XMV(f, t, 0);
  };
  for (let r = 0; r < 10; r++)
    for (let fl = 0; fl < 9; fl++) {
      const s = XSQ(fl, r), pc = b[s];
      if (!pc || XCOLOR(pc) !== c) continue;
      switch (XTYPE(pc)) {
      case XKING:
        for (let k = 0; k < 4; k++) if (in_palace(c, s + ORTH[k])) add(s, s + ORTH[k]);
        break;
      case XADVISOR:
        for (let k = 0; k < 4; k++) if (in_palace(c, s + DIAG[k])) add(s, s + DIAG[k]);
        break;
      case XELEPHANT:
        for (let k = 0; k < 4; k++) {
          const t = s + 2 * DIAG[k];
          if (b[t] !== XOFF && own_half(c, t) && !b[s + DIAG[k]]) add(s, t);
        }
        break;
      case XHORSE:
        for (let k = 0; k < 8; k++) if (!b[s + HL[k]]) add(s, s + HD[k]);
        break;
      case XCHARIOT:
        for (let k = 0; k < 4; k++) {
          const d = ORTH[k];
          let t = s + d;
          while (!b[t]) { if (!caps_only) ml[n++] = XMV(s, t, 0); t += d; }
          if (b[t] !== XOFF && XCOLOR(b[t]) !== c) ml[n++] = XMV(s, t, XF_CAP);
        }
        break;
      case XCANNON:
        for (let k = 0; k < 4; k++) {
          const d = ORTH[k];
          let t = s + d;
          while (!b[t]) { if (!caps_only) ml[n++] = XMV(s, t, 0); t += d; }
          if (b[t] === XOFF) continue;
          t += d;
          while (!b[t]) t += d;
          if (b[t] !== XOFF && XCOLOR(b[t]) !== c) ml[n++] = XMV(s, t, XF_CAP);
        }
        break;
      case XPAWN:
        add(s, s + (c === XRED ? 16 : -16));
        if (!own_half(c, s)) { add(s, s + 1); add(s, s - 1); }
        break;
      }
    }
  return n - base;
}

function xq_make_move(p, n, m) {
  copyPos(n, p);
  const f = XFROM(m), t = XTO(m), pc = n.b[f], cap = n.b[t];
  let i = ZI(pc, f), j = ZI(pc, t);
  n.hi ^= zpH[i] ^ zpH[j] ^ zsH; n.lo ^= zpL[i] ^ zpL[j] ^ zsL;
  if (cap) { i = ZI(cap, t); n.hi ^= zpH[i]; n.lo ^= zpL[i]; }
  n.b[t] = pc;
  n.b[f] = 0;
  if (XTYPE(pc) === XKING) setK(n, p.side, t);
  n.half = cap ? 0 : n.half < 255 ? n.half + 1 : 255;
  if (p.side === XBLACK) n.fullmove++;
  n.side ^= 1;
  return !xq_attacked(n, ksq(n, p.side), n.side);
}

const lq = newPos();
function xq_gen_legal(p, ml) {
  const tmp = new Uint32Array(MAXMOVES);
  const n = xq_gen_moves(p, tmp, 0, 0);
  let k = 0;
  for (let i = 0; i < n; i++) if (xq_make_move(p, lq, tmp[i])) ml[k++] = tmp[i];
  return k;
}

function xq_perft(p, depth) {
  const ml = new Uint32Array(MAXMOVES), c = newPos();
  const n = xq_gen_moves(p, ml, 0, 0);
  let t = 0;
  for (let i = 0; i < n; i++) {
    if (!xq_make_move(p, c, ml[i])) continue;
    t += depth > 1 ? xq_perft(c, depth - 1) : 1;
  }
  return t;
}

/* ---- notation ---- */
function xq_move_to_str(m, rank0) {
  const f = XFROM(m), t = XTO(m), add1 = rank0 ? 0 : 1;
  return String.fromCharCode(97 + XFILE(f)) + (XRANK(f) + add1) + String.fromCharCode(97 + XFILE(t)) + (XRANK(t) + add1);
}

function parse_with(p, s, add1) {
  const mm = /^([a-i])(\d+)([a-i])(\d+)/.exec(s);
  if (!mm) return 0;
  const r0 = +mm[2] - add1, r1 = +mm[4] - add1;
  if (r0 < 0 || r0 > 9 || r1 < 0 || r1 > 9) return 0;
  const a = XSQ(mm[1].charCodeAt(0) - 97, r0), b = XSQ(mm[3].charCodeAt(0) - 97, r1);
  const ml = new Uint32Array(MAXMOVES);
  const n = xq_gen_legal(p, ml);
  for (let i = 0; i < n; i++) if (XFROM(ml[i]) === a && XTO(ml[i]) === b) return ml[i];
  return 0;
}
function xq_str_to_move(p, s) { return parse_with(p, s, 0) || parse_with(p, s, 1); }

function xq_move_to_wxf(p, m) {
  const W = ' KAEHRCP';
  const f = XFROM(m), t = XTO(m), pc = p.b[f], c = XCOLOR(pc), ty = XTYPE(pc);
  const ff = XFILE(f), rf = XRANK(f), ft = XFILE(t), rt = XRANK(t);
  const fnum = c === XRED ? 9 - ff : ff + 1, tnum = c === XRED ? 9 - ft : ft + 1;
  let s = W[ty];
  let front = 0, other = 0;
  for (let r = 0; r < 10; r++) {
    if (r === rf || p.b[XSQ(ff, r)] !== pc) continue;
    other = 1;
    if (c === XRED ? r > rf : r < rf) front = 1;
  }
  if (other && ty !== XADVISOR && ty !== XELEPHANT) s += front ? '-' : '+'; else s += fnum;
  const fwd = c === XRED ? rt - rf : rf - rt;
  s += fwd > 0 ? '+' : fwd < 0 ? '-' : '=';
  if (ty === XADVISOR || ty === XELEPHANT || ty === XHORSE || fwd === 0) s += tnum;
  else s += fwd > 0 ? fwd : -fwd;
  return s;
}

/* ---- game state, evaluation ---- */
/* keys: [hi, lo] pairs of the game so far, the current position last */
function xq_game_state(p, keys) {
  const ml = new Uint32Array(MAXMOVES);
  if (!xq_gen_legal(p, ml)) return xq_in_check(p) ? 1 : 2;
  if (p.half >= 120) return 3;
  let att = 0;
  for (let r = 0; r < 10 && !att; r++)
    for (let f = 0; f < 9; f++) {
      const ty = XTYPE(p.b[XSQ(f, r)]);
      if (ty === XHORSE || ty === XCHARIOT || ty === XCANNON || ty === XPAWN) { att = 1; break; }
    }
  if (!att) return 4;
  let same = 0;
  for (let i = keys.length - 1, k = 0; i >= 0 && k <= p.half; i--, k++) if (keys[i][0] === p.hi && keys[i][1] === p.lo) same++;
  return same >= 3 ? 5 : 0;
}

const XVAL = [0, 0, 200, 200, 400, 900, 450, 100];
function pst(ty, rr, f) {
  const cen = 4 - (f > 4 ? f - 4 : 4 - f);
  switch (ty) {
  case XPAWN:
    if (rr <= 4) return rr === 4 ? 10 : 0;
    return 90 + (rr <= 7 ? 10 * cen : rr === 8 ? 5 * cen : 0) + (rr === 6 || rr === 7 ? 20 : 0);
  case XHORSE:   return 8 * cen + (rr >= 3 && rr <= 7 ? 15 : 0) - (rr === 0 ? 15 : 0);
  case XCANNON:  return (f === 4 ? 15 : 0) + (rr === 2 || rr === 7 ? 5 : 0);
  case XCHARIOT: return (rr >= 5 ? 10 : 0) + 2 * cen;
  case XKING:    return f === 4 ? 10 : 0;
  default:       return 0;
  }
}
const XE_MOB_R = 3, XE_MOB_H = 10, XE_MOB_C = 2, XE_PHASE = 6, XE_ATT = 14, XE_CCN = 25;   /* as src/xq_engine.c */

/* the attack on the general of side by ^ 1: chariots, horses, cannons and soldiers close to its palace */
function attack(p, by) {
  const g = ksq(p, by ^ 1), gf = XFILE(g), gr = XRANK(g);
  let units = 0;
  for (let r = 0; r < 10; r++)
    for (let f = 0; f < 9; f++) {
      const pc = p.b[XSQ(f, r)];
      if (!pc || XCOLOR(pc) !== by) continue;
      const df = f > gf ? f - gf : gf - f, dr = r > gr ? r - gr : gr - r, ty = XTYPE(pc);
      if (ty === XCHARIOT && (df <= 1 || dr <= 1)) units += 3;
      else if (ty === XHORSE && df + dr <= 4) units += 2;
      else if (ty === XCANNON && (df === 0 || dr === 0)) units += 2;
      else if (ty === XPAWN && df + dr <= 2) units += 2;
    }
  return Math.trunc(units * units * XE_ATT / 4);
}

function hce_evaluate(p) {
  const b = p.b, sc = [0, 0], guard = [0, 0];
  for (let r = 0; r < 10; r++)
    for (let f = 0; f < 9; f++) {
      const s = XSQ(f, r), pc = b[s];
      if (!pc) continue;
      const c = XCOLOR(pc), ty = XTYPE(pc), rr = c === XRED ? r : 9 - r;
      sc[c] += XVAL[ty] + pst(ty, rr, f);
      if (ty === XADVISOR || ty === XELEPHANT) guard[c]++;
      else if (ty === XCHARIOT) {
        let n = 0;
        for (let k = 0; k < 4; k++) {
          const d = ORTH[k];
          let t = s + d;
          while (!b[t]) { n++; t += d; }
          if (b[t] !== XOFF && XCOLOR(b[t]) !== c) n++;
        }
        sc[c] += n * XE_MOB_R;
      } else if (ty === XHORSE) {
        let n = 0;
        for (let k = 0; k < 8; k++) {
          const t = s + HD[k];
          if (!b[s + HL[k]] && b[t] !== XOFF && (!b[t] || XCOLOR(b[t]) !== c)) n++;
        }
        sc[c] += n * XE_MOB_H - 16;
      } else if (ty === XCANNON) {
        let n = 0;
        for (let k = 0; k < 4; k++) {
          const d = ORTH[k];
          let t = s + d;
          while (!b[t]) { n++; t += d; }
          if (b[t] === XOFF) continue;
          t += d;
          while (!b[t]) t += d;
          if (b[t] !== XOFF && XCOLOR(b[t]) !== c) n++;
        }
        sc[c] += n * XE_MOB_C;
        const g = ksq(p, c ^ 1);
        if (XFILE(g) === f) {
          const d = XRANK(g) > r ? 16 : -16;
          let t = s + d, between = 0;
          while (t !== g) { if (b[t]) between++; t += d; }
          if (between === 1) sc[c] += XE_CCN;
        }
      }
    }
  if (XE_PHASE) {                                       /* the cannon with many pieces, the horse in the endgame */
    let maj = 0;
    const nc = [0, 0], nh = [0, 0];
    for (let r = 0; r < 10; r++)
      for (let f = 0; f < 9; f++) {
        const pc = b[XSQ(f, r)], ty = XTYPE(pc);
        if (!pc) continue;
        if (ty === XCHARIOT || ty === XHORSE || ty === XCANNON) maj++;
        if (ty === XCANNON) nc[XCOLOR(pc)]++;
        if (ty === XHORSE) nh[XCOLOR(pc)]++;
      }
    const adj = (maj - 6) * XE_PHASE;
    sc[0] += (nc[0] - nh[0]) * adj;
    sc[1] += (nc[1] - nh[1]) * adj;
  }
  sc[0] -= (4 - guard[0]) * 15;
  sc[1] -= (4 - guard[1]) * 15;
  sc[0] += Math.trunc(attack(p, XRED) * (8 - guard[1]) / 6);
  sc[1] += Math.trunc(attack(p, XBLACK) * (8 - guard[0]) / 6);
  const v = sc[0] - sc[1];
  return (p.side === XRED ? v : -v) + 10;
}

/* ---- the evaluation network (src/nnue_xq.h, tools/nnue/train.py): as the C, number for number ----
   The weights come from xq (the page: base64 by tools/build_web.py; the tests: from the header). Each ply of
   the search keeps its accumulators with the key of the position they belong to; a position's come from the nearest
   ply below whose are current, by the squares that changed between the boards. */
const NN = (() => {
  const src = G.NNUE47 && G.NNUE47.xq;
  if (!src) return null;
  const dec = x => typeof x !== 'string' ? Int16Array.from(x) : (() => {
    const s = typeof atob === 'function' ? atob(x) : Buffer.from(x, 'base64').toString('binary');
    const u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i);
    return new Int16Array(u.buffer);
  })();
  return { hid: src.hid, qa: src.qa, qb: src.qb, scale: src.scale, w1: dec(src.w1), b1: dec(src.b1), w2: dec(src.w2), b2: src.b2 };
})();
let eval_hand = 0;                                           /* this search: the hand-written evaluation */
const nn_acc = NN ? Array.from({ length: MAXPLY + 2 }, () => [new Int16Array(NN.hid), new Int16Array(NN.hid)]) : null;
const nn_kH = new Int32Array(MAXPLY + 2), nn_kL = new Int32Array(MAXPLY + 2), nn_ok = new Uint8Array(MAXPLY + 2);
for (let i = 0; i < MAXPLY + 2; i++) stk[i].ply = i;
function nn_feat(pc, sq, view) {
  let col = XCOLOR(pc), r = XRANK(sq);
  if (view) { col ^= 1; r = 9 - r; }
  return ((col * 7 + XTYPE(pc) - 1) * 90 + r * 9 + XFILE(sq)) * NN.hid;
}
function nn_refresh(p, a) {
  const H = NN.hid;
  for (let v = 0; v < 2; v++) {
    a[v].set(NN.b1);
    for (let s = 0; s < 256; s++) {
      if (!p.b[s] || (p.b[s] & 0x80)) continue;            /* empty, or off the board (Xiangqi) */
      const o = nn_feat(p.b[s], s, v), av = a[v];
      for (let i = 0; i < H; i++) av[i] += NN.w1[o + i];
    }
  }
}
function nn_update(o, n, src, dst) {
  const H = NN.hid;
  dst[0].set(src[0]); dst[1].set(src[1]);
  for (let s = 0; s < 256; s++) {
    const a = o.b[s], b = n.b[s];
    if (a === b) continue;
    for (let v = 0; v < 2; v++) {
      const dv = dst[v];
      if (a) { const f = nn_feat(a, s, v); for (let i = 0; i < H; i++) dv[i] -= NN.w1[f + i]; }
      if (b) { const f = nn_feat(b, s, v); for (let i = 0; i < H; i++) dv[i] += NN.w1[f + i]; }
    }
  }
}
function nn_output(a, side) {
  const H = NN.hid, us = a[side], them = a[side ^ 1], qa = NN.qa, w2 = NN.w2;
  let out = NN.b2;
  for (let i = 0; i < H; i++) {
    const x = us[i] < 0 ? 0 : us[i] > qa ? qa : us[i], y = them[i] < 0 ? 0 : them[i] > qa ? qa : them[i];
    out += x * w2[i] + y * w2[H + i];
  }
  return Math.trunc(out * NN.scale / (NN.qa * NN.qb));
}
const nn_tmp = NN ? [new Int16Array(NN.hid), new Int16Array(NN.hid)] : null;
function xq_evaluate(p) {
  if (!NN || eval_hand) return hce_evaluate(p);               /* eval_hand: levels 1-3 */
  const ply = p.ply;
  if (ply === undefined || stk[ply] !== p) { nn_refresh(p, nn_tmp); return nn_output(nn_tmp, p.side); }
  if (!nn_ok[ply] || nn_kH[ply] !== p.hi || nn_kL[ply] !== p.lo) {
    let k = ply - 1;
    while (k >= 0 && (!nn_ok[k] || nn_kH[k] !== stk[k].hi || nn_kL[k] !== stk[k].lo)) k--;
    if (k < 0) { nn_refresh(stk[0], nn_acc[0]); nn_kH[0] = stk[0].hi; nn_kL[0] = stk[0].lo; nn_ok[0] = 1; k = 0; }
    for (; k < ply; k++) {
      nn_update(stk[k], stk[k + 1], nn_acc[k], nn_acc[k + 1]);
      nn_kH[k + 1] = stk[k + 1].hi; nn_kL[k + 1] = stk[k + 1].lo; nn_ok[k + 1] = 1;
    }
  }
  return nn_output(nn_acc[ply], p.side);
}

/* ---- the search: src/search.inc with Xiangqi's hooks ---- */
const ORDV = [0, 9, 2, 2, 4, 6, 5, 1];
const SP_RFP = 90, SP_FUT = 150, SP_DELTA = 200, SP_LMP = 4, SP_ASP = 25;  /* as src/search.inc */
/* the late move reductions (src/lmr.h): 0.75 + ln(depth) ln(move number) / 2.25 (test/test_web.js: the C's table) */
const LMR_T = new Uint8Array(32 * 64);
for (let d = 1; d < 32; d++) for (let m = 1; m < 64; m++) LMR_T[d * 64 + m] = Math.floor(0.75 + Math.log(d) * Math.log(m) / 2.25);
/* the history with gravity (src/search.inc hist_add) */
function hist_add(idx, bonus) {
  let v = hh[idx];
  v += bonus - ((v * (bonus < 0 ? -bonus : bonus) / 16384) | 0);
  hh[idx] = v > 16000 ? 16000 : v < -16000 ? -16000 : v;
}
let nodes = 0, stop = 0, t0 = 0, L = null, root_best = 0, root_score = 0;

function check_stop() {
  if (L.max_nodes && nodes >= L.max_nodes) { stop = 1; return; }
  if (L.poll && L.poll()) { stop = 1; return; }
  if (L.time_ms && L.now && (L.now() - t0) >= L.time_ms) stop = 1;
}
function is_rep(p) {
  for (let i = rep_n - 3, k = 2; i >= 0 && k <= p.half; i -= 2, k += 2) if (repH[i] === p.hi && repL[i] === p.lo) return 1;
  return 0;
}
const score_to_tt = (s, ply) => s > MATE - 2 * MAXPLY ? s + ply : s < -MATE + 2 * MAXPLY ? s - ply : s;
const score_from_tt = (s, ply) => s > MATE - 2 * MAXPLY ? s - ply : s < -MATE + 2 * MAXPLY ? s + ply : s;
const HIDX = (p, m) => (p.b[XFROM(m)] << 8) | XTO(m);

/* a capture likely to lose material (src/xq_engine.c xq_badcap) */
function badcap(p, m) {
  const att = XTYPE(p.b[XFROM(m)]), vic = XTYPE(p.b[XTO(m)]);
  if (att === XKING) return 0;
  return XVAL[att] - XVAL[vic] > 50 && xq_attacked(p, XTO(m), p.side ^ 1) ? 1 : 0;
}
function order(p, o, n, ttm, ply) {
  for (let i = o; i < o + n; i++) {
    const m = mbuf[i];
    let v;
    if (m === ttm) v = 1 << 30;
    else if (XFLAGS(m) & XF_CAP) {
      v = 1000000 + ORDV[XTYPE(p.b[XTO(m)])] * 100 - ORDV[XTYPE(p.b[XFROM(m)])];
      if (badcap(p, m)) v -= 3000000;
    }
    else if (m === killer[ply * 2]) v = 900000;
    else if (m === killer[ply * 2 + 1]) v = 800000;
    else v = hh[HIDX(p, m)];
    sbuf[i] = v;
  }
}
function pick(o, n, i) {
  let bi = o + i;
  for (let j = o + i + 1; j < o + n; j++) if (sbuf[j] > sbuf[bi]) bi = j;
  const m = mbuf[bi], s = sbuf[bi];
  mbuf[bi] = mbuf[o + i]; sbuf[bi] = sbuf[o + i];
  mbuf[o + i] = m; sbuf[o + i] = s;
  return m;
}

function qsearch(ply, alpha, beta) {
  const p = stk[ply];
  if ((++nodes & 1023) === 0) check_stop();
  if (stop) return 0;
  {                                                      /* the hash table's bounds (src/search.inc SP_QTT) */
    const e = p.lo & (tt_n - 1);
    if (ttH[e] === p.hi && ttL[e] === p.lo && ttD[e] >= 0 && beta - alpha === 1) {
      const s = score_from_tt(ttS[e], ply), f = ttF[e] & 3;
      if (f === TT_EXACT || (f === TT_LOWER && s >= beta) || (f === TT_UPPER && s <= alpha)) return s;
    }
  }
  const stand = xq_evaluate(p);
  if (ply >= MAXPLY - 1) return stand;
  if (stand >= beta) return stand;
  if (stand > alpha) alpha = stand;
  if (mofs[ply] + MAXMOVES > MBUF) return stand;
  const o = mofs[ply];
  const n = xq_gen_moves(p, mbuf, 1, o);
  mofs[ply + 1] = o + n;
  order(p, o, n, 0, ply);
  for (let i = 0; i < n; i++) {
    const m = pick(o, n, i);
    if (stand + XVAL[XTYPE(p.b[XTO(m)])] + SP_DELTA < alpha) continue;
    if (sbuf[o + i] < 0) continue;                       /* a losing capture */
    if (!xq_make_move(p, stk[ply + 1], m)) continue;
    const s = -qsearch(ply + 1, -beta, -alpha);
    if (stop) return 0;
    if (s >= beta) return s;
    if (s > alpha) alpha = s;
  }
  return alpha;
}

function has_pieces(p) {
  for (let r = 0; r < 10; r++)
    for (let f = 0; f < 9; f++) {
      const pc = p.b[XSQ(f, r)];
      if (pc && XCOLOR(pc) === p.side && (XTYPE(pc) === XCHARIOT || XTYPE(pc) === XHORSE || XTYPE(pc) === XCANNON))
        return 1;
    }
  return 0;
}
function hash32(x) {
  x >>>= 0;
  x ^= x >>> 16; x = Math.imul(x, 0x7feb352d); x ^= x >>> 15; x = Math.imul(x, 0x846ca68b | 0); x ^= x >>> 16;
  return x >>> 0;
}

function negamax(ply, depth, alpha, beta, can_null) {
  const p = stk[ply];
  if ((++nodes & 1023) === 0) check_stop();
  if (stop) return 0;
  if (ply && (p.half >= 120 || is_rep(p))) return 0;
  if (ply >= MAXPLY - 2) return xq_evaluate(p);
  if (ply) {                                              /* mate distance pruning */
    if (alpha < -MATE + ply) alpha = -MATE + ply;
    if (beta > MATE - ply - 1) beta = MATE - ply - 1;
    if (alpha >= beta) return alpha;
  }
  const incheck = xq_in_check(p);
  if (incheck) depth++;
  if (depth <= 0) return qsearch(ply, alpha, beta);

  const e = p.lo & (tt_n - 1);
  let ttm = 0;
  if (ttH[e] === p.hi && ttL[e] === p.lo) {
    ttm = ttM[e];
    if (ply && ttD[e] >= depth) {
      const s = score_from_tt(ttS[e], ply);
      if (ttF[e] === TT_EXACT) return s;
      if (ttF[e] === TT_LOWER && s >= beta) return s;
      if (ttF[e] === TT_UPPER && s <= alpha) return s;
    }
  }

  /* near the leaves, out of the principal variation: the static score decides what is worth searching */
  const pv = beta - alpha > 1;
  const st = incheck ? -INF : xq_evaluate(p);
  if (!pv && !incheck && ply && depth <= 3 && beta < MATE - 2 * MAXPLY && st - SP_RFP * depth >= beta) return st;
  const fut = !pv && !incheck && depth <= 2 && alpha > -MATE + 2 * MAXPLY && st + SP_FUT * depth <= alpha;

  if (can_null && !incheck && ply && depth >= 3 && beta < MATE - 2 * MAXPLY && has_pieces(p) && st >= beta) {
    const n = copyPos(stk[ply + 1], p);
    n.side ^= 1; n.hi ^= zsH; n.lo ^= zsL; n.half = 0;
    repH[rep_n] = n.hi; repL[rep_n] = n.lo; rep_n++;
    mofs[ply + 1] = mofs[ply];                           /* the child's list starts here (not at a stale offset) */
    const R = depth > 6 ? 3 : 2;
    const s = -negamax(ply + 1, depth - 1 - R, -beta, -beta + 1, 0);
    rep_n--;
    if (stop) return 0;
    if (s >= beta) return s >= MATE - 2 * MAXPLY ? beta : s;
  }

  if (mofs[ply] + MAXMOVES > MBUF) return xq_evaluate(p);
  const o = mofs[ply];
  const n = xq_gen_moves(p, mbuf, 0, o);
  mofs[ply + 1] = o + n;
  order(p, o, n, ttm, ply);

  let best = -INF, legal = 0, bm = 0;
  const orig = alpha;
  for (let i = 0; i < n; i++) {
    const m = pick(o, n, i);
    const c = stk[ply + 1];
    if (!xq_make_move(p, c, m)) continue;
    legal++;
    repH[rep_n] = c.hi; repL[rep_n] = c.lo; rep_n++;
    const quiet = !(XFLAGS(m) & XF_CAP);
    const gives = xq_in_check(c);
    /* futility and late move pruning: quiet moves that give no check, near the leaves, after the first */
    if (legal > 1 && quiet && !gives && !pv && !incheck && m !== killer[ply * 2] && m !== killer[ply * 2 + 1] &&
        (fut || (depth <= 3 && legal > SP_LMP + 2 * depth * depth))) { rep_n--; continue; }
    let nz = 0;
    if (ply === 0 && L.noise) nz = (hash32((m ^ L.seed) >>> 0) % (2 * L.noise + 1)) - L.noise;
    const a = alpha - nz, bt = beta - nz;
    let s;
    if (legal === 1) s = -negamax(ply + 1, depth - 1, -bt, -a, 1);
    else {
      let r = 0;
      if (depth >= 3 && legal > 2 && quiet && !incheck && !gives) {
        r = LMR_T[(depth > 31 ? 31 : depth) * 64 + (legal > 63 ? 63 : legal)];
        if (pv) r--;
        if (m === killer[ply * 2] || m === killer[ply * 2 + 1]) r--;
        r -= (hh[HIDX(p, m)] / 8192) | 0;
        if (r < 0) r = 0;
        if (r > depth - 2) r = depth - 2;
      }
      s = -negamax(ply + 1, depth - 1 - r, -a - 1, -a, 1);
      if (s > a && r) s = -negamax(ply + 1, depth - 1, -a - 1, -a, 1);
      if (s > a && s < bt) s = -negamax(ply + 1, depth - 1, -bt, -a, 1);
    }
    rep_n--;
    if (stop) return 0;
    s += nz;
    if (s > best) {
      best = s; bm = m;
      if (ply === 0) { root_best = m; root_score = s; }
      if (s > alpha) {
        alpha = s;
        if (s >= beta) {
          if (quiet) {
            if (killer[ply * 2] !== m) { killer[ply * 2 + 1] = killer[ply * 2]; killer[ply * 2] = m; }
            const bonus = depth > 13 ? 1600 : 8 * depth * depth + 16 * depth;
            for (let j = 0; j < i; j++) {                 /* the quiet moves tried before it lose as much */
              const q = mbuf[o + j];
              if (!(XFLAGS(q) & XF_CAP)) hist_add(HIDX(p, q), -bonus);
            }
            hist_add(HIDX(p, m), bonus);
          }
          break;
        }
      }
    }
  }
  if (!legal) return -MATE + ply;                       /* Xiangqi: no legal move loses, in check or not */

  if (pv && bm && best > orig && best < beta) {          /* a main-line node: its move kept for the display */
    const q = p.lo & (PVH_N - 1);                         /* the deeper kept (nearer the root), this search's */
    if (pvhA[q] !== pv_age || (pvhH[q] === p.hi && pvhL[q] === p.lo) || depth >= pvhD[q]) {
      pvhH[q] = p.hi; pvhL[q] = p.lo; pvhM[q] = bm; pvhD[q] = depth; pvhA[q] = pv_age;
    }
  }
  if (ttH[e] !== p.hi || ttL[e] !== p.lo || depth >= ttD[e] || best >= beta) {
    ttH[e] = p.hi; ttL[e] = p.lo; ttM[e] = bm;
    ttS[e] = score_to_tt(best, ply);
    ttD[e] = depth;
    ttF[e] = best >= beta ? TT_LOWER : best <= orig ? TT_UPPER : TT_EXACT;
  }
  return best;
}

/* the main lines' best moves, apart from the hash table (src/engine_common.h PVE): the "thinking" display's line */
const PVH_N = 512;
const pvhH = new Int32Array(PVH_N), pvhL = new Int32Array(PVH_N), pvhM = new Uint32Array(PVH_N);
const pvhD = new Int8Array(PVH_N), pvhA = new Uint8Array(PVH_N);
let pv_age = 0;                                           /* the search's number: older entries give way */
/* the expected line: first, then each position's main-line move (else the hash table's), legal, until a repeat
   (src/search.inc search_pv): the tables are only read */
function xq_search_pv(root, first, max) {
  const pv = [], seen = new Set();
  let p = copyPos(newPos(), root), m = first;
  while (m && pv.length < max && pv.length < 16) {
    const n = newPos();
    if (!xq_make_move(p, n, m)) break;
    const k = n.hi + ',' + n.lo;
    if (seen.has(k)) break;
    seen.add(k); pv.push(m); p = n;
    const q = p.lo & (PVH_N - 1), e = p.lo & (tt_n - 1);
    m = pvhH[q] === p.hi && pvhL[q] === p.lo ? pvhM[q] : ttH[e] === p.hi && ttL[e] === p.lo ? ttM[e] : 0;
  }
  return pv;
}

/* lim: { depth, time_ms, noise, seed, max_nodes, hand_eval, now(), poll(), info(depth, score, nodes, best) };
   keys: [hi, lo] pairs of the game so far */
function xq_search(root, keys, lim) {
  const R = { best: 0, score: 0, depth: 0, nodes: 0 };
  L = lim; eval_hand = lim.hand_eval ? 1 : 0; stop = 0; nodes = 0; pv_age = (pv_age + 1) & 255;
  t0 = lim.now ? lim.now() : 0;
  const n = xq_gen_legal(root, scratch);
  if (!n) return R;
  R.best = scratch[0];
  const first = keys.length > MAXGAME ? keys.length - MAXGAME : 0;
  rep_n = 0;
  for (let i = first; i < keys.length; i++) { repH[rep_n] = keys[i][0]; repL[rep_n] = keys[i][1]; rep_n++; }
  if (!rep_n || repH[rep_n - 1] !== root.hi || repL[rep_n - 1] !== root.lo) { repH[rep_n] = root.hi; repL[rep_n] = root.lo; rep_n++; }
  for (let i = 0; i < 16 * 256; i++) hh[i] >>= 2;
  killer.fill(0);
  copyPos(stk[0], root);
  mofs[0] = 0;
  const maxd = lim.depth < 1 ? 1 : lim.depth > MAXPLY - 8 ? MAXPLY - 8 : lim.depth;
  for (let d = 1; d <= maxd; d++) {
    let s, a = -INF, b = INF, delta = SP_ASP;
    if (d >= 5 && !IS_MATE(R.score)) { a = R.score - delta; b = R.score + delta; }   /* aspiration window */
    for (;;) {
      root_best = 0;
      s = negamax(0, d, a, b, 1);
      if (stop) {
        if (root_best && root_score > a) { R.best = root_best; R.score = root_score; R.depth = d; }
        break;
      }
      if (s <= a && a > -INF) { b = ((a + b) / 2) | 0; a = s - delta < -INF ? -INF : s - delta; }
      else if (s >= b && b < INF) { if (root_best) { R.best = root_best; R.score = s; } b = s + delta > INF ? INF : s + delta; }
      else { if (root_best) { R.best = root_best; R.score = s; R.depth = d; } break; }
      delta += delta;
      if (delta > 600) { a = -INF; b = INF; }
    }
    if (stop) break;
    if (lim.info) lim.info(d, R.score, nodes, R.best);
    if (n === 1 && d >= 2) break;
    if (IS_MATE(s) && d >= 6) break;
    if (lim.time_ms && lim.now && (lim.now() - t0) > lim.time_ms / 2) break;
  }
  R.nodes = nodes;
  return R;
}

Object.assign(X, {
  MAXMOVES, MAXGAME, INF, MATE, IS_MATE, XF_CAP, XMV, XFROM, XTO, XFLAGS, XEMPTY, XKING, XADVISOR, XELEPHANT, XHORSE,
  XCHARIOT, XCANNON, XPAWN, XRED, XBLACK, XOFF, XTYPE, XCOLOR, XSQ, XFILE, XRANK, newPos, copyPos, ksq, xq_init,
  xq_clear_tt, xq_from_fen, xq_to_fen, xq_pos_start, xq_attacked, xq_in_check, xq_gen_moves, xq_gen_legal,
  xq_make_move, xq_perft, xq_move_to_str, xq_str_to_move, xq_move_to_wxf, xq_game_state, xq_evaluate, xq_search, xq_search_pv,
});
G.Xiangqi47 = X;
if (typeof module !== 'undefined') module.exports = X;
})(typeof self !== 'undefined' ? self : globalThis);
