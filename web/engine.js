/*
 * CHESS47 web - the engine of src/engine.c in JavaScript, line for line
 * (C) 2026 Victor Valdes - GNU GPLv3
 *
 * 0x88 board, copy-make, legal move generation with Chess960 castling, PVS alpha-beta with a transposition
 * table, null move, LMR and quiescence. The 64-bit Zobrist keys are two 32-bit halves (hi, lo), made by the
 * same xorshift64 as the C, so the keys, the node counts and the moves are the C engine's
 * (test/test_web.js checks perft and fixed-depth searches against test/jscheck.c).
 */
'use strict';
(function (G) {
const MAXPLY = 64, MAXMOVES = 256, MAXGAME = 400;
const F_CAP = 1, F_EP = 2, F_CASTLE = 4, F_DPUSH = 8, F_QSIDE = 16;
const EMPTY = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6;
const WHITE = 0, BLACK = 1, BLK = 8, NOSQ = 0x7f;
const INF = 32000, MATE = 31000;
const MV = (f, t, pr, fl) => (f | (t << 8) | (pr << 16) | (fl << 24)) >>> 0;
const MFROM = m => m & 0x7f, MTO = m => (m >>> 8) & 0x7f, MPROMO = m => (m >>> 16) & 0xf, MFLAGS = m => m >>> 24;
const PTYPE = p => p & 7, PCOLOR = p => p >> 3;
const SQ = (f, r) => (r << 4) | f, FILE_OF = s => s & 7, RANK_OF = s => s >> 4;
const ONBOARD = s => !(s & 0x88);
const IS_MATE = s => s > MATE - MAXPLY * 2 || s < -MATE + MAXPLY * 2;

const E = {};                                          /* the engine's shared state (c_rfile, chess960) */
E.c_rfile = [7, 0];
E.chess960 = 0;

/* ---- positions: b[128], side, castle, ep, half, fullmove, k0 k1 (the kings), hi lo (the key) ---- */
function newPos() { return { b: new Uint8Array(128), side: 0, castle: 0, ep: NOSQ, half: 0, fullmove: 1, k0: 0, k1: 0, hi: 0, lo: 0 }; }
function copyPos(d, s) {
  d.b.set(s.b); d.side = s.side; d.castle = s.castle; d.ep = s.ep; d.half = s.half; d.fullmove = s.fullmove;
  d.k0 = s.k0; d.k1 = s.k1; d.hi = s.hi; d.lo = s.lo; return d;
}
const ksq = (p, c) => c ? p.k1 : p.k0;
function setK(p, c, s) { if (c) p.k1 = s; else p.k0 = s; }

const castle_mask = new Uint8Array(128);
const zpH = new Int32Array(16 * 128), zpL = new Int32Array(16 * 128);
const zcH = new Int32Array(16), zcL = new Int32Array(16), zeH = new Int32Array(8), zeL = new Int32Array(8);
let zsH = 0, zsL = 0;

let tt_n = 1 << 20;                                    /* entries: the C's with tt_bytes 16 MB (16-byte entries) */
let ttH, ttL, ttM, ttS, ttD, ttF;
const TT_LOWER = 1, TT_UPPER = 2, TT_EXACT = 3;

const MBUF = MAXPLY * 96;
const stk = []; for (let i = 0; i < MAXPLY + 2; i++) stk.push(newPos());
const mbuf = new Uint32Array(MBUF), sbuf = new Int32Array(MBUF);
const mofs = new Uint16Array(MAXPLY + 2);
const killer = new Uint32Array(MAXPLY * 2);
const hh = new Int16Array(16 * 128);
const repH = new Int32Array(MAXGAME + MAXPLY + 8), repL = new Int32Array(MAXGAME + MAXPLY + 8);
let rep_n = 0;
const scratch = new Uint32Array(MAXMOVES);

const NDIR = [33, 31, 18, 14, -33, -31, -18, -14];
const KDIR = [1, -1, 16, -16, 17, 15, -17, -15];

/* ---- init: the C's xorshift64 from 0x9E3779B97F4A7C15 ---- */
function eng_init(entries) {
  if (entries) tt_n = entries;
  ttH = new Int32Array(tt_n); ttL = new Int32Array(tt_n); ttM = new Uint32Array(tt_n);
  ttS = new Int16Array(tt_n); ttD = new Int8Array(tt_n); ttF = new Uint8Array(tt_n);
  let rs = 0x9E3779B97F4A7C15n;
  const M = (1n << 64n) - 1n;
  const next = () => { let x = rs; x ^= (x << 13n) & M; x ^= x >> 7n; x ^= (x << 17n) & M; rs = x; return x; };
  const put = (H, L, i) => { const x = next(); H[i] = Number(x >> 32n) | 0; L[i] = Number(x & 0xffffffffn) | 0; };
  for (let i = 0; i < 16 * 128; i++) put(zpH, zpL, i);
  for (let i = 0; i < 16; i++) put(zcH, zcL, i);
  for (let i = 0; i < 8; i++) put(zeH, zeL, i);
  const z = next(); zsH = Number(z >> 32n) | 0; zsL = Number(z & 0xffffffffn) | 0;
  eng_clear_tt();
}
function eng_clear_tt() { ttH.fill(0); ttL.fill(0); ttM.fill(0); ttS.fill(0); ttD.fill(0); ttF.fill(0); hh.fill(0); killer.fill(0); }

/* ---- position helpers ---- */
function compute_key(p) {
  let h = 0, l = 0;
  for (let s = 0; s < 128; s++) if (ONBOARD(s) && p.b[s]) { const i = (p.b[s] << 7) | s; h ^= zpH[i]; l ^= zpL[i]; }
  h ^= zcH[p.castle]; l ^= zcL[p.castle];
  if (p.ep !== NOSQ) { h ^= zeH[FILE_OF(p.ep)]; l ^= zeL[FILE_OF(p.ep)]; }
  if (p.side) { h ^= zsH; l ^= zsL; }
  p.hi = h; p.lo = l;
}

function setup_castle_mask(p) {
  castle_mask.fill(0xF);
  const r = E.c_rfile;
  if (p.castle & 3) { castle_mask[p.k0] &= ~3; castle_mask[SQ(r[0], 0)] &= ~1; castle_mask[SQ(r[1], 0)] &= ~2; }
  if (p.castle & 12) { castle_mask[p.k1] &= ~12; castle_mask[SQ(r[0], 7)] &= ~4; castle_mask[SQ(r[1], 7)] &= ~8; }
}

function attacked(p, sq, by) {
  const b = p.b, c = by << 3;
  let t;
  if (by === WHITE) {
    t = sq - 15; if (ONBOARD(t) && b[t] === (PAWN | c)) return 1;
    t = sq - 17; if (ONBOARD(t) && b[t] === (PAWN | c)) return 1;
  } else {
    t = sq + 15; if (ONBOARD(t) && b[t] === (PAWN | c)) return 1;
    t = sq + 17; if (ONBOARD(t) && b[t] === (PAWN | c)) return 1;
  }
  for (let i = 0; i < 8; i++) {
    t = sq + NDIR[i]; if (ONBOARD(t) && b[t] === (KNIGHT | c)) return 1;
    t = sq + KDIR[i]; if (ONBOARD(t) && b[t] === (KING | c)) return 1;
  }
  for (let i = 0; i < 8; i++) {
    const d = KDIR[i], diag = i >= 4;
    for (t = sq + d; ONBOARD(t); t += d) {
      const pc = b[t];
      if (!pc) continue;
      if (PCOLOR(pc) === by) { const ty = PTYPE(pc); if (ty === QUEEN || ty === (diag ? BISHOP : ROOK)) return 1; }
      break;
    }
  }
  return 0;
}
const in_check = p => attacked(p, ksq(p, p.side), p.side ^ 1);

function insufficient(p) {
  let minors = 0;
  for (let s = 0; s < 128; s++) {
    if (!ONBOARD(s) || !p.b[s]) continue;
    const t = PTYPE(p.b[s]);
    if (t === PAWN || t === ROOK || t === QUEEN) return 0;
    if (t === KNIGHT || t === BISHOP) minors++;
  }
  return minors <= 1 ? 1 : 0;
}

/* ---- FEN ---- */
const PCH = 'PNBRQKpnbrqk';
function pos_from_fen(p, f) {
  p.b.fill(0); p.side = 0; p.castle = 0; p.ep = NOSQ; p.half = 0; p.fullmove = 1; p.k0 = p.k1 = 0;
  const parts = f.trim().split(/\s+/);
  let r = 7, fl = 0, kings = 0;
  for (const ch of parts[0] || '') {
    if (ch === '/') { r--; fl = 0; continue; }
    if (ch >= '1' && ch <= '8') { fl += +ch; continue; }
    const i = PCH.indexOf(ch);
    if (i < 0 || fl > 7 || r < 0) return 0;
    const pc = (i % 6 + 1) | (i >= 6 ? BLK : 0);
    p.b[SQ(fl, r)] = pc;
    if (PTYPE(pc) === KING) { setK(p, PCOLOR(pc), SQ(fl, r)); kings |= 1 << PCOLOR(pc); }
    fl++;
  }
  if (kings !== 3) return 0;
  p.side = parts[1] === 'b' ? 1 : 0;
  E.c_rfile[0] = 7; E.c_rfile[1] = 0;
  let x960 = 0;
  for (const ch of parts[2] || '-') {
    if (ch === '-') continue;
    const col = ch >= 'a' && ch <= 'z' ? 1 : 0, rank = col ? 7 : 0, u = ch.toUpperCase();
    const kf = FILE_OF(ksq(p, col)), rook = ROOK | (col << 3);
    if (RANK_OF(ksq(p, col)) !== rank) continue;
    if (u === 'K') {
      for (let x = 7; x > kf; x--) if (p.b[SQ(x, rank)] === rook) { E.c_rfile[0] = x; p.castle |= col ? 4 : 1; break; }
    } else if (u === 'Q') {
      for (let x = 0; x < kf; x++) if (p.b[SQ(x, rank)] === rook) { E.c_rfile[1] = x; p.castle |= col ? 8 : 2; break; }
    } else if (u >= 'A' && u <= 'H') {
      const x = u.charCodeAt(0) - 65;
      x960 = 1;
      if (p.b[SQ(x, rank)] !== rook) continue;
      if (x > kf) { E.c_rfile[0] = x; p.castle |= col ? 4 : 1; } else { E.c_rfile[1] = x; p.castle |= col ? 8 : 2; }
    }
  }
  if (x960) E.chess960 = 1;
  const ep = parts[3] || '-';
  if (/^[a-h][1-8]$/.test(ep)) p.ep = SQ(ep.charCodeAt(0) - 97, ep.charCodeAt(1) - 49);
  const h = parseInt(parts[4], 10), fm = parseInt(parts[5], 10);
  p.half = isNaN(h) ? 0 : Math.min(h, 255);
  p.fullmove = isNaN(fm) || fm < 1 ? 1 : fm;
  setup_castle_mask(p);
  compute_key(p);
  if (attacked(p, ksq(p, p.side ^ 1), p.side)) return 0;
  return 1;
}

function pos_to_fen(p) {
  let o = '';
  for (let r = 7; r >= 0; r--) {
    let e = 0;
    for (let f = 0; f < 8; f++) {
      const pc = p.b[SQ(f, r)];
      if (!pc) { e++; continue; }
      if (e) { o += e; e = 0; }
      o += PCH[(PTYPE(pc) - 1) + (PCOLOR(pc) ? 6 : 0)];
    }
    if (e) o += e;
    if (r) o += '/';
  }
  o += ' ' + (p.side ? 'b' : 'w') + ' ';
  if (!p.castle) o += '-';
  else for (let i = 0; i < 4; i++) if (p.castle & (1 << i))
    o += E.chess960 ? String.fromCharCode((i < 2 ? 65 : 97) + E.c_rfile[i & 1]) : 'KQkq'[i];
  o += ' ' + (p.ep !== NOSQ ? String.fromCharCode(97 + FILE_OF(p.ep)) + (RANK_OF(p.ep) + 1) : '-');
  return o + ' ' + p.half + ' ' + p.fullmove;
}

function pos_start(p) { E.chess960 = 0; pos_from_fen(p, 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'); }

function pos_960(p, n) {
  const KN = [[0,1],[0,2],[0,3],[0,4],[1,2],[1,3],[1,4],[2,3],[2,4],[3,4]];
  const row = new Array(8).fill('');
  if (n < 0 || n > 959) n = 518;
  let v = n;
  row[2 * (v % 4) + 1] = 'B'; v = Math.floor(v / 4);
  row[2 * (v % 4)] = 'B'; v = Math.floor(v / 4);
  const q = v % 6; v = Math.floor(v / 6);
  let empt = []; for (let i = 0; i < 8; i++) if (!row[i]) empt.push(i);
  row[empt[q]] = 'Q';
  empt = []; for (let i = 0; i < 8; i++) if (!row[i]) empt.push(i);
  row[empt[KN[v][0]]] = 'N'; row[empt[KN[v][1]]] = 'N';
  const rkr = 'RKR';
  for (let i = 0, j = 0; i < 8; i++) if (!row[i]) row[i] = rkr[j++];
  pos_from_fen(p, row.join('').toLowerCase() + '/pppppppp/8/8/8/8/PPPPPPPP/' + row.join('') + ' w KQkq - 0 1');
  E.chess960 = 1;
}

/* ---- move generation ---- */
const ctmp = newPos();
function castle_ok(p, qs) {
  const us = p.side, r = us ? 0x70 : 0;
  const right = us ? (qs ? 8 : 4) : (qs ? 2 : 1);
  if (!(p.castle & right)) return 0;
  const kf = ksq(p, us), rf = r | E.c_rfile[qs];
  const kt = r | (qs ? 2 : 6), rt = r | (qs ? 3 : 5);
  if (p.b[rf] !== (ROOK | (us << 3))) return 0;
  const lo = Math.min(kf, kt, rf, rt), hi = Math.max(kf, kt, rf, rt);
  for (let s = lo; s <= hi; s++) if (s !== kf && s !== rf && p.b[s]) return 0;
  copyPos(ctmp, p);
  ctmp.b[kf] = 0; ctmp.b[rf] = 0;
  const a = Math.min(kf, kt), z = Math.max(kf, kt);
  for (let s = a; s <= z; s++) if (attacked(ctmp, s, us ^ 1)) return 0;
  return 1;
}

function add_pawn(ml, n, f, t, fl, caps_only) {
  if (RANK_OF(t) === 0 || RANK_OF(t) === 7) {
    ml[n++] = MV(f, t, QUEEN, fl);
    if (!caps_only) { ml[n++] = MV(f, t, ROOK, fl); ml[n++] = MV(f, t, BISHOP, fl); ml[n++] = MV(f, t, KNIGHT, fl); }
  } else if (!caps_only || (fl & F_CAP)) ml[n++] = MV(f, t, 0, fl);
  return n;
}

function gen_moves(p, ml, caps_only, base) {
  const b = p.b, us = p.side, them = us ^ 1;
  let n = base | 0;
  const up = us ? -16 : 16, start = us ? 6 : 1;
  for (let s = 0; s < 128; s++) {
    if (s & 0x88) { s += 7; continue; }
    const pc = b[s];
    if (!pc || PCOLOR(pc) !== us) continue;
    const ty = PTYPE(pc);
    let t;
    if (ty === PAWN) {
      t = s + up;
      if (!b[t]) {
        n = add_pawn(ml, n, s, t, 0, caps_only);
        if (!caps_only && RANK_OF(s) === start && !b[t + up]) ml[n++] = MV(s, t + up, 0, F_DPUSH);
      }
      for (let dc = -1; dc <= 1; dc += 2) {
        t = s + up + dc;
        if (!ONBOARD(t)) continue;
        if (b[t] && PCOLOR(b[t]) === them) n = add_pawn(ml, n, s, t, F_CAP, caps_only);
        else if (t === p.ep) ml[n++] = MV(s, t, 0, F_CAP | F_EP);
      }
    } else if (ty === KNIGHT || ty === KING) {
      const D = ty === KNIGHT ? NDIR : KDIR;
      for (let i = 0; i < 8; i++) {
        t = s + D[i];
        if (!ONBOARD(t)) continue;
        if (!b[t]) { if (!caps_only) ml[n++] = MV(s, t, 0, 0); }
        else if (PCOLOR(b[t]) === them) ml[n++] = MV(s, t, 0, F_CAP);
      }
      if (ty === KING && !caps_only) {
        if (castle_ok(p, 0)) ml[n++] = MV(s, (us ? 0x70 : 0) | 6, 0, F_CASTLE);
        if (castle_ok(p, 1)) ml[n++] = MV(s, (us ? 0x70 : 0) | 2, 0, F_CASTLE | F_QSIDE);
      }
    } else {
      const i0 = ty === BISHOP ? 4 : 0, i1 = ty === ROOK ? 4 : 8;
      for (let i = i0; i < i1; i++) {
        const d = KDIR[i];
        for (t = s + d; ONBOARD(t); t += d) {
          if (!b[t]) { if (!caps_only) ml[n++] = MV(s, t, 0, 0); continue; }
          if (PCOLOR(b[t]) === them) ml[n++] = MV(s, t, 0, F_CAP);
          break;
        }
      }
    }
  }
  return n - (base | 0);
}

function make_move(p, n, m) {
  copyPos(n, p);
  const us = p.side, them = us ^ 1;
  const from = MFROM(m), to = MTO(m), fl = MFLAGS(m);
  const pc = n.b[from];
  let h = n.hi, l = n.lo, i;
  if (n.ep !== NOSQ) { h ^= zeH[FILE_OF(n.ep)]; l ^= zeL[FILE_OF(n.ep)]; n.ep = NOSQ; }
  h ^= zcH[n.castle]; l ^= zcL[n.castle];
  if (n.half < 255) n.half++;
  if (fl & F_CASTLE) {
    const r = us ? 0x70 : 0, qs = fl & F_QSIDE ? 1 : 0;
    const rf = r | E.c_rfile[qs], kt = r | (qs ? 2 : 6), rt = r | (qs ? 3 : 5);
    const rook = n.b[rf];
    n.b[from] = 0; i = (pc << 7) | from; h ^= zpH[i]; l ^= zpL[i];
    n.b[rf] = 0; i = (rook << 7) | rf; h ^= zpH[i]; l ^= zpL[i];
    n.b[kt] = pc; i = (pc << 7) | kt; h ^= zpH[i]; l ^= zpL[i];
    n.b[rt] = rook; i = (rook << 7) | rt; h ^= zpH[i]; l ^= zpL[i];
    setK(n, us, kt);
    n.castle &= us ? ~12 & 0xff : ~3 & 0xff;
  } else {
    if (fl & F_EP) {
      const cs = us === WHITE ? to - 16 : to + 16;
      i = (n.b[cs] << 7) | cs; h ^= zpH[i]; l ^= zpL[i];
      n.b[cs] = 0; n.half = 0;
    } else if (n.b[to]) { i = (n.b[to] << 7) | to; h ^= zpH[i]; l ^= zpL[i]; n.half = 0; }
    const np = MPROMO(m) ? (MPROMO(m) | (us << 3)) : pc;
    n.b[from] = 0; i = (pc << 7) | from; h ^= zpH[i]; l ^= zpL[i];
    n.b[to] = np; i = (np << 7) | to; h ^= zpH[i]; l ^= zpL[i];
    if (PTYPE(pc) === PAWN) {
      n.half = 0;
      if (fl & F_DPUSH) {
        const e = (from + to) >> 1, ep_pawn = PAWN | (them << 3);
        if ((ONBOARD(to - 1) && n.b[to - 1] === ep_pawn) || (ONBOARD(to + 1) && n.b[to + 1] === ep_pawn)) {
          n.ep = e; h ^= zeH[FILE_OF(e)]; l ^= zeL[FILE_OF(e)];
        }
      }
    }
    if (PTYPE(pc) === KING) setK(n, us, to);
    n.castle &= castle_mask[from] & castle_mask[to];
  }
  h ^= zcH[n.castle]; l ^= zcL[n.castle];
  n.side = them;
  h ^= zsH; l ^= zsL;
  if (us === BLACK) n.fullmove++;
  n.hi = h; n.lo = l;
  return attacked(n, ksq(n, us), them) ? 0 : 1;
}

const lq = newPos();
function gen_legal(p, ml) {
  const n = gen_moves(p, ml, 0, 0);
  let k = 0;
  for (let i = 0; i < n; i++) if (make_move(p, lq, ml[i])) ml[k++] = ml[i];
  return k;
}

function perft(p, depth) {
  const ml = new Uint32Array(MAXMOVES), q = newPos();
  const n = gen_moves(p, ml, 0, 0);
  let c = 0;
  for (let i = 0; i < n; i++) {
    if (!make_move(p, q, ml[i])) continue;
    c += depth > 1 ? perft(q, depth - 1) : 1;
  }
  return c;
}

const castle_rook_sq = (p, m) => (p.side ? 0x70 : 0) | E.c_rfile[MFLAGS(m) & F_QSIDE ? 1 : 0];

/* ---- notation ---- */
const sqname = s => String.fromCharCode(97 + FILE_OF(s)) + (RANK_OF(s) + 1);
function move_to_str(p, m) {
  let to = MTO(m);
  if (MFLAGS(m) & F_CASTLE) to = castle_rook_sq(p, m);
  return sqname(MFROM(m)) + sqname(to) + (MPROMO(m) ? ' pnbrqk'[MPROMO(m)] : '');
}
function str_to_move(p, s) {
  const ml = new Uint32Array(MAXMOVES), n = gen_legal(p, ml);
  s = s.trim();
  for (let i = 0; i < n; i++) {
    const b = move_to_str(p, ml[i]);
    if (b === s || (b.length === 5 && b === s.slice(0, 5)) || (b.length === 4 && s.slice(0, 4) === b && s.length === 4)) return ml[i];
  }
  for (let i = 0; i < n; i++) {                       /* king-destination castling (e1g1) */
    const m = ml[i];
    if (!(MFLAGS(m) & F_CASTLE)) continue;
    if (s.slice(0, 4) === sqname(MFROM(m)) + sqname(MTO(m))) return m;
  }
  return 0;
}
const sq_ = newPos();
function move_to_san(p, m) {
  let o = '';
  const fl = MFLAGS(m);
  if (fl & F_CASTLE) o = fl & F_QSIDE ? 'O-O-O' : 'O-O';
  else {
    const from = MFROM(m), to = MTO(m), ty = PTYPE(p.b[from]);
    if (ty === PAWN) { if (fl & F_CAP) o += String.fromCharCode(97 + FILE_OF(from)) + 'x'; }
    else {
      o += ' PNBRQK'[ty];
      const ml = new Uint32Array(MAXMOVES), n = gen_legal(p, ml);
      let same = 0, sf = 0, sr = 0;
      for (let i = 0; i < n; i++) {
        const x = ml[i];
        if (x === m || MTO(x) !== to || (MFLAGS(x) & F_CASTLE)) continue;
        if (PTYPE(p.b[MFROM(x)]) !== ty) continue;
        same = 1;
        if (FILE_OF(MFROM(x)) === FILE_OF(from)) sf = 1;
        if (RANK_OF(MFROM(x)) === RANK_OF(from)) sr = 1;
      }
      if (same) o += !sf ? String.fromCharCode(97 + FILE_OF(from)) : !sr ? String(RANK_OF(from) + 1) : sqname(from);
      if (fl & F_CAP) o += 'x';
    }
    o += sqname(to);
    if (MPROMO(m)) o += '=' + ' PNBRQK'[MPROMO(m)];
  }
  make_move(p, sq_, m);
  if (in_check(sq_)) o += gen_legal(sq_, new Uint32Array(MAXMOVES)) ? '+' : '#';
  return o;
}

/* 0 playing, 1 checkmate, 2 stalemate, 3 fifty moves, 4 insufficient material, 5 threefold repetition;
   keys: [hi, lo] pairs of the game so far (the last one the position p) */
function game_state(p, keys) {
  if (!gen_legal(p, scratch)) return in_check(p) ? 1 : 2;
  if (p.half >= 100) return 3;
  if (insufficient(p)) return 4;
  let cnt = 1;
  for (let i = keys.length - 3, k = 2; i >= 0 && k <= p.half; i -= 2, k += 2)
    if (keys[i][0] === p.hi && keys[i][1] === p.lo && ++cnt >= 3) return 5;
  return 0;
}

/* ---- evaluation ---- */
const VAL = [0, 100, 320, 330, 500, 900, 0];
const PST = [
  new Array(64).fill(0),
  [ 0, 0, 0, 0, 0, 0, 0, 0, 50,50,50,50,50,50,50,50, 10,10,20,30,30,20,10,10, 5, 5,10,25,25,10, 5, 5,
    0, 0, 0,20,20, 0, 0, 0, 5,-5,-10,0, 0,-10,-5, 5, 5,10,10,-20,-20,10,10, 5, 0, 0, 0, 0, 0, 0, 0, 0],
  [-50,-40,-30,-30,-30,-30,-40,-50, -40,-20,0,0,0,0,-20,-40, -30,0,10,15,15,10,0,-30, -30,5,15,20,20,15,5,-30,
   -30,0,15,20,20,15,0,-30, -30,5,10,15,15,10,5,-30, -40,-20,0,5,5,0,-20,-40, -50,-40,-30,-30,-30,-30,-40,-50],
  [-20,-10,-10,-10,-10,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,10,10,5,0,-10, -10,5,5,10,10,5,5,-10,
   -10,0,10,10,10,10,0,-10, -10,10,10,10,10,10,10,-10, -10,5,0,0,0,0,5,-10, -20,-10,-10,-10,-10,-10,-10,-20],
  [ 0,0,0,0,0,0,0,0, 5,10,10,10,10,10,10,5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5,
   -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, 0,0,0,5,5,0,0,0],
  [-20,-10,-10,-5,-5,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,5,5,5,0,-10, -5,0,5,5,5,5,0,-5,
    0,0,5,5,5,5,0,-5, -10,5,5,5,5,5,0,-10, -10,0,5,0,0,0,0,-10, -20,-10,-10,-5,-5,-10,-10,-20],
  [-30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30, -20,-30,-30,-40,-40,-30,-30,-20, -10,-20,-20,-20,-20,-20,-20,-10,
    20,20,0,0,0,0,20,20, 20,30,10,0,0,10,30,20],
];
const KING_EG = [-50,-40,-30,-20,-20,-30,-40,-50, -30,-20,-10,0,0,-10,-20,-30, -30,-10,20,30,30,20,-10,-30,
  -30,-10,30,40,40,30,-10,-30, -30,-10,30,40,40,30,-10,-30, -30,-10,20,30,30,20,-10,-30, -30,-30,0,0,0,0,-30,-30,
  -50,-30,-30,-30,-30,-30,-30,-50];
const PASS_MG = [0, 5, 10, 15, 25, 40, 60, 0], PASS_EG = [0, 10, 20, 35, 60, 100, 150, 0];
const PHASE = [0, 0, 1, 1, 2, 4, 0];
const pcnt = [new Int8Array(10), new Int8Array(10)], pmin = [new Int8Array(10), new Int8Array(10)], pmax = [new Int8Array(10), new Int8Array(10)];

function evaluate(p) {
  const b = p.b, mg = [0, 0], eg = [0, 0], bish = [0, 0];
  let phase = 0;
  for (let c = 0; c < 2; c++) { pcnt[c].fill(0); pmin[c].fill(8); pmax[c].fill(-1); }
  for (let s = 0; s < 128; s++) {
    if (s & 0x88) { s += 7; continue; }
    const pc = b[s];
    if (!pc) continue;
    const c = PCOLOR(pc), ty = PTYPE(pc), f = FILE_OF(s), r = RANK_OF(s);
    const idx = c ? r * 8 + f : (7 - r) * 8 + f;
    phase += PHASE[ty];
    if (ty === KING) { mg[c] += PST[KING][idx]; eg[c] += KING_EG[idx]; continue; }
    mg[c] += VAL[ty] + PST[ty][idx];
    eg[c] += VAL[ty] + PST[ty][idx];
    if (ty === PAWN) {
      pcnt[c][f + 1]++;
      if (r < pmin[c][f + 1]) pmin[c][f + 1] = r;
      if (r > pmax[c][f + 1]) pmax[c][f + 1] = r;
    } else {
      if (ty === BISHOP) bish[c]++;
      let mob = 0;
      if (ty === KNIGHT) {
        for (let i = 0; i < 8; i++) { const t = s + NDIR[i]; if (ONBOARD(t) && (!b[t] || PCOLOR(b[t]) !== c)) mob++; }
        mg[c] += 4 * (mob - 4); eg[c] += 4 * (mob - 4);
      } else {
        const i0 = ty === BISHOP ? 4 : 0, i1 = ty === ROOK ? 4 : 8;
        for (let i = i0; i < i1; i++)
          for (let t = s + KDIR[i]; ONBOARD(t); t += KDIR[i]) {
            if (!b[t]) { mob++; continue; }
            if (PCOLOR(b[t]) !== c) mob++;
            break;
          }
        if (ty === BISHOP) { mg[c] += 5 * (mob - 6); eg[c] += 5 * (mob - 6); }
        else if (ty === ROOK) { mg[c] += 2 * (mob - 7); eg[c] += 4 * (mob - 7); }
        else { mg[c] += mob - 14; eg[c] += 2 * (mob - 14); }
      }
    }
  }
  for (let s = 0; s < 128; s++) {
    if (s & 0x88) { s += 7; continue; }
    const pc = b[s];
    if (!pc) continue;
    const c = PCOLOR(pc), ty = PTYPE(pc), f = FILE_OF(s) + 1, r = RANK_OF(s);
    if (ty === PAWN) {
      if (!pcnt[c][f - 1] && !pcnt[c][f + 1]) { mg[c] -= 10; eg[c] -= 15; }
      const passed = c === WHITE ? pmax[1][f - 1] <= r && pmax[1][f] <= r && pmax[1][f + 1] <= r
                                 : pmin[0][f - 1] >= r && pmin[0][f] >= r && pmin[0][f + 1] >= r;
      if (passed) { const rr = c ? 7 - r : r; mg[c] += PASS_MG[rr]; eg[c] += PASS_EG[rr]; }
    } else if (ty === ROOK) {
      if (!pcnt[c][f]) {
        if (!pcnt[c ^ 1][f]) { mg[c] += 20; eg[c] += 10; } else { mg[c] += 10; eg[c] += 5; }
      }
    }
  }
  for (let c = 0; c < 2; c++) {
    for (let f = 1; f <= 8; f++) if (pcnt[c][f] > 1) { mg[c] -= 10 * (pcnt[c][f] - 1); eg[c] -= 20 * (pcnt[c][f] - 1); }
    if (bish[c] >= 2) { mg[c] += 30; eg[c] += 50; }
    const home = c ? 7 : 0, fwd = c ? -1 : 1;
    for (let f = 0; f < 8; f++) {
      const pc = b[SQ(f, home)];
      if (pc && PCOLOR(pc) === c && (PTYPE(pc) === KNIGHT || PTYPE(pc) === BISHOP)) mg[c] -= 12;
    }
    for (let f = 0; f < 8; f += 7) {
      const moved = pcnt[c][f + 1] && (c ? pmin[1][f + 1] < 5 : pmax[0][f + 1] > 2);
      if (moved) mg[c] -= 10;
    }
    const ks = ksq(p, c), kf = FILE_OF(ks), kr = RANK_OF(ks);
    if (kr === home) {
      let shield = 0;
      for (let f = kf - 1; f <= kf + 1; f++) {
        if (f < 0 || f > 7) continue;
        const a = b[SQ(f, kr + fwd)], a2 = b[SQ(f, kr + 2 * fwd)];
        if (a === (PAWN | (c << 3))) shield += 12;
        else if (a2 === (PAWN | (c << 3))) shield += 6;
      }
      if (kf >= 3 && kf <= 4) shield -= 10;
      mg[c] += shield;
    }
  }
  if (phase > 24) phase = 24;
  const m = mg[0] - mg[1], e = eg[0] - eg[1];
  const sc = Math.trunc((m * phase + e * (24 - phase)) / 24);
  return (p.side === WHITE ? sc : -sc) + 10;
}

/* ---- search ---- */
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

function order(p, o, n, ttm, ply) {
  for (let i = o; i < o + n; i++) {
    const m = mbuf[i];
    let v;
    if (m === ttm) v = 1 << 30;
    else if (MFLAGS(m) & F_CAP) {
      const vic = MFLAGS(m) & F_EP ? PAWN : PTYPE(p.b[MTO(m)]);
      v = 1000000 + vic * 100 - PTYPE(p.b[MFROM(m)]);
    } else if (MPROMO(m) === QUEEN) v = 950000;
    else if (m === killer[ply * 2]) v = 900000;
    else if (m === killer[ply * 2 + 1]) v = 800000;
    else v = hh[(p.b[MFROM(m)] << 7) | MTO(m)];
    if (MPROMO(m) && MPROMO(m) !== QUEEN) v -= 2000000;
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
  const stand = evaluate(p);
  if (ply >= MAXPLY - 1) return stand;
  if (stand >= beta) return stand;
  if (stand > alpha) alpha = stand;
  if (mofs[ply] + MAXMOVES > MBUF) return stand;
  const o = mofs[ply];
  const n = gen_moves(p, mbuf, 1, o);
  mofs[ply + 1] = o + n;
  order(p, o, n, 0, ply);
  for (let i = 0; i < n; i++) {
    const m = pick(o, n, i);
    if (!MPROMO(m)) {
      const vic = MFLAGS(m) & F_EP ? PAWN : PTYPE(p.b[MTO(m)]);
      if (stand + VAL[vic] + 200 < alpha) continue;
    }
    if (!make_move(p, stk[ply + 1], m)) continue;
    const s = -qsearch(ply + 1, -beta, -alpha);
    if (stop) return 0;
    if (s >= beta) return s;
    if (s > alpha) alpha = s;
  }
  return alpha;
}

function has_pieces(p) {
  const c = p.side;
  for (let s = 0; s < 128; s++) {
    if (s & 0x88) { s += 7; continue; }
    const pc = p.b[s];
    if (pc && PCOLOR(pc) === c && PTYPE(pc) !== PAWN && PTYPE(pc) !== KING) return 1;
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
  if (ply && (p.half >= 100 || is_rep(p))) return 0;
  if (ply >= MAXPLY - 2) return evaluate(p);
  const incheck = in_check(p);
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

  if (can_null && !incheck && ply && depth >= 3 && beta < MATE - 2 * MAXPLY && has_pieces(p) && evaluate(p) >= beta) {
    const n = copyPos(stk[ply + 1], p);
    n.side ^= 1; n.hi ^= zsH; n.lo ^= zsL;
    if (n.ep !== NOSQ) { n.hi ^= zeH[FILE_OF(n.ep)]; n.lo ^= zeL[FILE_OF(n.ep)]; n.ep = NOSQ; }
    n.half = 0;
    repH[rep_n] = n.hi; repL[rep_n] = n.lo; rep_n++;
    const R = depth > 6 ? 3 : 2;
    const s = -negamax(ply + 1, depth - 1 - R, -beta, -beta + 1, 0);
    rep_n--;
    if (stop) return 0;
    if (s >= beta) return s >= MATE - 2 * MAXPLY ? beta : s;
  }

  if (mofs[ply] + MAXMOVES > MBUF) return evaluate(p);
  const o = mofs[ply];
  const n = gen_moves(p, mbuf, 0, o);
  mofs[ply + 1] = o + n;
  order(p, o, n, ttm, ply);

  let best = -INF, legal = 0, bm = 0;
  const orig = alpha;
  for (let i = 0; i < n; i++) {
    const m = pick(o, n, i);
    const c = stk[ply + 1];
    if (!make_move(p, c, m)) continue;
    legal++;
    repH[rep_n] = c.hi; repL[rep_n] = c.lo; rep_n++;
    const quiet = !(MFLAGS(m) & F_CAP) && !MPROMO(m);
    const gives = in_check(c);
    let nz = 0;
    if (ply === 0 && L.noise) nz = (hash32((m ^ L.seed) >>> 0) % (2 * L.noise + 1)) - L.noise;
    const a = alpha - nz, bt = beta - nz;
    let s;
    if (legal === 1) s = -negamax(ply + 1, depth - 1, -bt, -a, 1);
    else {
      let r = 0;
      if (depth >= 3 && legal > 3 && quiet && !incheck && !gives && m !== killer[ply * 2] && m !== killer[ply * 2 + 1])
        r = legal > 10 ? 2 : 1;
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
            const idx = (p.b[MFROM(m)] << 7) | MTO(m);
            const v = hh[idx] + depth * depth;
            hh[idx] = v > 30000 ? 30000 : v;
          }
          break;
        }
      }
    }
  }
  if (!legal) return incheck ? -MATE + ply : 0;

  if (ttH[e] !== p.hi || ttL[e] !== p.lo || depth >= ttD[e] || best >= beta) {
    ttH[e] = p.hi; ttL[e] = p.lo; ttM[e] = bm;
    ttS[e] = score_to_tt(best, ply);
    ttD[e] = depth;
    ttF[e] = best >= beta ? TT_LOWER : best <= orig ? TT_UPPER : TT_EXACT;
  }
  return best;
}

/* lim: { depth, time_ms, noise, seed, max_nodes, now(), poll(), info(depth, score, nodes, best) };
   keys: [hi, lo] pairs of the game so far */
function search(root, keys, lim) {
  const R = { best: 0, score: 0, depth: 0, nodes: 0 };
  L = lim; stop = 0; nodes = 0;
  t0 = lim.now ? lim.now() : 0;
  const n = gen_legal(root, scratch);
  if (!n) return R;
  R.best = scratch[0];
  const first = keys.length > MAXGAME ? keys.length - MAXGAME : 0;
  rep_n = 0;
  for (let i = first; i < keys.length; i++) { repH[rep_n] = keys[i][0]; repL[rep_n] = keys[i][1]; rep_n++; }
  if (!rep_n || repH[rep_n - 1] !== root.hi || repL[rep_n - 1] !== root.lo) { repH[rep_n] = root.hi; repL[rep_n] = root.lo; rep_n++; }
  for (let i = 0; i < 16 * 128; i++) hh[i] >>= 2;
  killer.fill(0);
  copyPos(stk[0], root);
  mofs[0] = 0;
  const maxd = lim.depth < 1 ? 1 : lim.depth > MAXPLY - 8 ? MAXPLY - 8 : lim.depth;
  for (let d = 1; d <= maxd; d++) {
    root_best = 0;
    const s = negamax(0, d, -INF, INF, 1);
    if (root_best) { R.best = root_best; R.score = stop ? root_score : s; R.depth = d; }
    if (stop) break;
    if (lim.info) lim.info(d, R.score, nodes, R.best);
    if (n === 1 && d >= 2) break;
    if (IS_MATE(s) && d >= 6) break;
    if (lim.time_ms && lim.now && (lim.now() - t0) > lim.time_ms / 2) break;
  }
  R.nodes = nodes;
  return R;
}

Object.assign(E, {
  MAXPLY, MAXMOVES, MAXGAME, F_CAP, F_EP, F_CASTLE, F_DPUSH, F_QSIDE, EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
  WHITE, BLACK, BLK, NOSQ, INF, MATE, MV, MFROM, MTO, MPROMO, MFLAGS, PTYPE, PCOLOR, SQ, FILE_OF, RANK_OF, ONBOARD,
  IS_MATE, newPos, copyPos, ksq, eng_init, eng_clear_tt, pos_from_fen, pos_to_fen, pos_start, pos_960, attacked,
  in_check, insufficient, gen_moves, gen_legal, make_move, perft, castle_rook_sq, move_to_str, str_to_move,
  move_to_san, game_state, evaluate, search, sqname,
});
G.Chess47 = E;
if (typeof module !== 'undefined') module.exports = E;
})(typeof self !== 'undefined' ? self : globalThis);
