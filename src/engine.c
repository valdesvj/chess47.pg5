/*
 * CHESS47 for DM42n (DMCP5) - portable chess engine
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 *
 * Memory note: DMCP5 gives a program only 8 KB of static RAM, so every
 * table here (Zobrist keys, hash table, search stacks) lives on the heap
 * and is allocated once by eng_init(). Recursive frames stay small.
 */
#include "engine.h"
#include <string.h>

/* the evaluation network, when tools/nnue/train.py has made one (NO_NNUE: the hand-written evaluation) */
#if !defined(NO_NNUE) && __has_include("nnue_chess.h")
#include "nnue_chess.h"
#include "kpk.h"
#define HAVE_NNUE 1
static int16_t (*nn_acc)[2][NNC_HID];        /* [MAXPLY + 2] the accumulators of the search's plies */
static uint64_t *nn_key;                     /* [MAXPLY + 2] the position each belongs to */
#endif
static int eval_hand;                        /* this search uses the hand-written evaluation (Limits.hand_eval) */

uint8_t c_rfile[2] = { 7, 0 };
int     chess960   = 0;

static uint8_t  castle_mask[128];
static uint64_t zcastle[16], zep[8], zside;
static uint64_t *zp;                  /* [16][128] */

typedef struct { uint64_t key; Move move; int16_t score; int8_t depth; uint8_t flag; } TTE;
#define TT_LOWER 1
#define TT_UPPER 2
#define TT_EXACT 3
static TTE     *tt;
static uint32_t tt_n;                 /* entries, power of two */

#define MBUF (MAXPLY * 96)
static Pos     *stk;                  /* [MAXPLY+2] search position stack */
static Move    *mbuf;                 /* shared move stack */
static int32_t *sbuf;                 /* move ordering scores */
static uint16_t mofs[MAXPLY + 2];
static Move   (*killer)[2];           /* [MAXPLY][2] */
static int16_t *hh;                   /* history [16*256]: chess uses 16*128, Xiangqi all (shared) */
static uint64_t *rep;                 /* repetition keys: game + search path */
static int      rep_n;
static Move    *scratch;              /* [MAXMOVES] for SAN etc. */
static PVE     *pvh;                  /* [PVH_N] the main lines (search.inc) */

static const int8_t NDIR[8] = { 33, 31, 18, 14, -33, -31, -18, -14 };
static const int8_t KDIR[8] = { 1, -1, 16, -16, 17, 15, -17, -15 };

#define ZP(pc, sq) zp[((pc) << 7) | (sq)]

/* ------------------------------------------------------------------ */
/*  Init                                                              */
/* ------------------------------------------------------------------ */

static uint64_t rs = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd64(void) { uint64_t x = rs; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return rs = x; }

int eng_init(void *(*alloc)(size_t), size_t tt_bytes) {
  zp      = alloc(16 * 128 * sizeof(uint64_t));
  stk     = alloc((MAXPLY + 2) * sizeof(Pos));
  mbuf    = alloc(MBUF * sizeof(Move));
  sbuf    = alloc(MBUF * sizeof(int32_t));
  killer  = alloc(MAXPLY * 2 * sizeof(Move));
  hh      = alloc(HH_N * sizeof(int16_t));
  rep     = alloc((MAXGAME + MAXPLY + 8) * sizeof(uint64_t));
  scratch = alloc(MAXMOVES * sizeof(Move));
  pvh     = alloc(PVH_N * sizeof(PVE));
#ifdef HAVE_NNUE
  nn_acc  = alloc((MAXPLY + 2) * sizeof *nn_acc);
  nn_key  = alloc((MAXPLY + 2) * sizeof(uint64_t));
  if (!nn_acc || !nn_key) return 0;
#endif
  if (!zp || !stk || !mbuf || !sbuf || !killer || !hh || !rep || !scratch || !pvh) return 0;

  tt_n = 1;
  while ((size_t)tt_n * 2 * sizeof(TTE) <= tt_bytes) tt_n *= 2;
  tt = NULL;
  while (tt_n >= 1024 && !(tt = alloc(tt_n * sizeof(TTE)))) tt_n /= 2;
  if (!tt) return 0;

  for (int i = 0; i < 16 * 128; i++) zp[i] = rnd64();
  for (int i = 0; i < 16; i++) zcastle[i] = rnd64();
  for (int i = 0; i < 8; i++) zep[i] = rnd64();
  zside = rnd64();
  eng_clear_tt();
  return 1;
}

size_t eng_tt_bytes(void) { return (size_t)tt_n * sizeof(TTE); }

void eng_clear_tt(void) {
  memset(tt, 0, tt_n * sizeof(TTE));
  memset(pvh, 0, PVH_N * sizeof(PVE));
  memset(hh, 0, HH_N * sizeof(int16_t));
  memset(killer, 0, MAXPLY * 2 * sizeof(Move));
}

/* ------------------------------------------------------------------ */
/*  Position helpers                                                  */
/* ------------------------------------------------------------------ */

static uint64_t compute_key(const Pos *p) {
  uint64_t k = 0;
  for (int s = 0; s < 128; s++)
    if (ONBOARD(s) && p->b[s]) k ^= ZP(p->b[s], s);
  k ^= zcastle[p->castle];
  if (p->ep != NOSQ) k ^= zep[FILE_OF(p->ep)];
  if (p->side) k ^= zside;
  return k;
}

static void setup_castle_mask(const Pos *p) {
  memset(castle_mask, 0xF, sizeof castle_mask);
  if (p->castle & 3) {
    castle_mask[p->ksq[WHITE]] &= ~3;
    castle_mask[SQ(c_rfile[0], 0)] &= ~1;
    castle_mask[SQ(c_rfile[1], 0)] &= ~2;
  }
  if (p->castle & 12) {
    castle_mask[p->ksq[BLACK]] &= ~12;
    castle_mask[SQ(c_rfile[0], 7)] &= ~4;
    castle_mask[SQ(c_rfile[1], 7)] &= ~8;
  }
}

int attacked(const Pos *p, int sq, int by) {
  const uint8_t *b = p->b;
  int t, c = by << 3;
  if (by == WHITE) {
    t = sq - 15; if (ONBOARD(t) && b[t] == (PAWN | c)) return 1;
    t = sq - 17; if (ONBOARD(t) && b[t] == (PAWN | c)) return 1;
  } else {
    t = sq + 15; if (ONBOARD(t) && b[t] == (PAWN | c)) return 1;
    t = sq + 17; if (ONBOARD(t) && b[t] == (PAWN | c)) return 1;
  }
  for (int i = 0; i < 8; i++) {
    t = sq + NDIR[i]; if (ONBOARD(t) && b[t] == (KNIGHT | c)) return 1;
    t = sq + KDIR[i]; if (ONBOARD(t) && b[t] == (KING | c)) return 1;
  }
  for (int i = 0; i < 8; i++) {
    int d = KDIR[i], diag = (i >= 4);
    for (t = sq + d; ONBOARD(t); t += d) {
      int pc = b[t];
      if (!pc) continue;
      if (PCOLOR(pc) == by) {
        int ty = PTYPE(pc);
        if (ty == QUEEN || ty == (diag ? BISHOP : ROOK)) return 1;
      }
      break;
    }
  }
  return 0;
}

int in_check(const Pos *p) { return attacked(p, p->ksq[p->side], p->side ^ 1); }

int insufficient(const Pos *p) {
  int minors = 0;
  for (int s = 0; s < 128; s++) {
    if (!ONBOARD(s) || !p->b[s]) continue;
    int t = PTYPE(p->b[s]);
    if (t == PAWN || t == ROOK || t == QUEEN) return 0;
    if (t == KNIGHT || t == BISHOP) minors++;
  }
  return minors <= 1;
}

/* ------------------------------------------------------------------ */
/*  FEN                                                               */
/* ------------------------------------------------------------------ */

static const char *PCH = "PNBRQKpnbrqk";

static const char *read_int(const char *f, int *v) {
  while (*f == ' ') f++;
  if (*f >= '0' && *f <= '9') { int x = 0; while (*f >= '0' && *f <= '9') x = x * 10 + (*f++ - '0'); *v = x; }
  return f;
}

int pos_from_fen(Pos *p, const char *f) {
  memset(p, 0, sizeof *p);
  p->ep = NOSQ;
  int r = 7, fl = 0, kings = 0;
  while (*f == ' ') f++;
  for (; *f && *f != ' '; f++) {
    char ch = *f;
    if (ch == '/') { r--; fl = 0; continue; }
    if (ch >= '1' && ch <= '8') { fl += ch - '0'; continue; }
    const char *q = strchr(PCH, ch);
    if (!q || fl > 7 || r < 0) return 0;
    int i = (int)(q - PCH);
    int pc = (i % 6 + 1) | (i >= 6 ? BLK : 0);
    p->b[SQ(fl, r)] = (uint8_t)pc;
    p->men++;
    if (PTYPE(pc) == KING) { p->ksq[PCOLOR(pc)] = (uint8_t)SQ(fl, r); kings |= 1 << PCOLOR(pc); }
    fl++;
  }
  if (kings != 3) return 0;
  while (*f == ' ') f++;
  p->side = (*f == 'b');
  if (*f) f++;
  while (*f == ' ') f++;

  c_rfile[0] = 7; c_rfile[1] = 0;
  int x960 = 0;
  for (; *f && *f != ' '; f++) {
    char ch = *f;
    if (ch == '-') continue;
    int col = (ch >= 'a' && ch <= 'z');
    int rank = col ? 7 : 0;
    char u = col ? (char)(ch - 32) : ch;
    int kf = FILE_OF(p->ksq[col]);
    int rook = ROOK | (col << 3);
    if (RANK_OF(p->ksq[col]) != rank) continue;
    if (u == 'K') {
      for (int x = 7; x > kf; x--)
        if (p->b[SQ(x, rank)] == rook) { c_rfile[0] = (uint8_t)x; p->castle |= col ? 4 : 1; break; }
    } else if (u == 'Q') {
      for (int x = 0; x < kf; x++)
        if (p->b[SQ(x, rank)] == rook) { c_rfile[1] = (uint8_t)x; p->castle |= col ? 8 : 2; break; }
    } else if (u >= 'A' && u <= 'H') {
      int x = u - 'A';
      x960 = 1;
      if (p->b[SQ(x, rank)] != rook) continue;
      if (x > kf) { c_rfile[0] = (uint8_t)x; p->castle |= col ? 4 : 1; }
      else        { c_rfile[1] = (uint8_t)x; p->castle |= col ? 8 : 2; }
    }
  }
  if (x960) chess960 = 1;
  while (*f == ' ') f++;
  if (*f >= 'a' && *f <= 'h' && f[1] >= '1' && f[1] <= '8') { p->ep = (uint8_t)SQ(f[0] - 'a', f[1] - '1'); f += 2; }
  else if (*f == '-') f++;
  int h = 0, fm = 1;
  f = read_int(f, &h);
  f = read_int(f, &fm);
  p->half = (uint8_t)(h > 255 ? 255 : h);
  p->fullmove = (uint16_t)(fm < 1 ? 1 : fm);
  setup_castle_mask(p);
  p->key = compute_key(p);
  if (attacked(p, p->ksq[p->side ^ 1], p->side)) return 0;  /* side not to move in check */
  return 1;
}

void pos_to_fen(const Pos *p, char *o) {
  for (int r = 7; r >= 0; r--) {
    int e = 0;
    for (int f = 0; f < 8; f++) {
      int pc = p->b[SQ(f, r)];
      if (!pc) { e++; continue; }
      if (e) { *o++ = (char)('0' + e); e = 0; }
      *o++ = PCH[(PTYPE(pc) - 1) + (PCOLOR(pc) ? 6 : 0)];
    }
    if (e) *o++ = (char)('0' + e);
    if (r) *o++ = '/';
  }
  *o++ = ' '; *o++ = p->side ? 'b' : 'w'; *o++ = ' ';
  if (!p->castle) *o++ = '-';
  else {
    static const uint8_t bit[4] = { 1, 2, 4, 8 };
    for (int i = 0; i < 4; i++) if (p->castle & bit[i]) {
      if (chess960) *o++ = (char)((i < 2 ? 'A' : 'a') + c_rfile[i & 1]);
      else *o++ = "KQkq"[i];
    }
  }
  *o++ = ' ';
  if (p->ep != NOSQ) { *o++ = (char)('a' + FILE_OF(p->ep)); *o++ = (char)('1' + RANK_OF(p->ep)); }
  else *o++ = '-';
  int n = p->half, m = p->fullmove;
  char tmp[8]; int k;
  *o++ = ' ';
  k = 0; do { tmp[k++] = (char)('0' + n % 10); n /= 10; } while (n); while (k) *o++ = tmp[--k];
  *o++ = ' ';
  k = 0; do { tmp[k++] = (char)('0' + m % 10); m /= 10; } while (m); while (k) *o++ = tmp[--k];
  *o = 0;
}

void pos_start(Pos *p) {
  chess960 = 0;
  pos_from_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void pos_960(Pos *p, int n) {
  static const uint8_t KN[10][2] = { {0,1},{0,2},{0,3},{0,4},{1,2},{1,3},{1,4},{2,3},{2,4},{3,4} };
  char row[8];
  memset(row, 0, sizeof row);
  if (n < 0 || n > 959) n = 518;
  int v = n;
  row[2 * (v % 4) + 1] = 'B'; v /= 4;
  row[2 * (v % 4)] = 'B';     v /= 4;
  int q = v % 6;              v /= 6;
  int empt[8], ne = 0;
  for (int i = 0; i < 8; i++) if (!row[i]) empt[ne++] = i;
  row[empt[q]] = 'Q';
  ne = 0;
  for (int i = 0; i < 8; i++) if (!row[i]) empt[ne++] = i;
  row[empt[KN[v][0]]] = 'N';
  row[empt[KN[v][1]]] = 'N';
  const char *rkr = "RKR";
  for (int i = 0, j = 0; i < 8; i++) if (!row[i]) row[i] = rkr[j++];

  char fen[96], *o = fen;
  for (int i = 0; i < 8; i++) *o++ = (char)(row[i] + 32);
  strcpy(o, "/pppppppp/8/8/8/8/PPPPPPPP/"); o += strlen(o);
  for (int i = 0; i < 8; i++) *o++ = row[i];
  strcpy(o, " w KQkq - 0 1");
  pos_from_fen(p, fen);
  chess960 = 1;
}

/* ------------------------------------------------------------------ */
/*  Move generation                                                   */
/* ------------------------------------------------------------------ */

static int castle_ok(const Pos *p, int qs) {
  int us = p->side, r = us ? 0x70 : 0;
  int right = us ? (qs ? 8 : 4) : (qs ? 2 : 1);
  if (!(p->castle & right)) return 0;
  int kf = p->ksq[us], rf = r | c_rfile[qs];
  int kt = r | (qs ? 2 : 6), rt = r | (qs ? 3 : 5);
  if (p->b[rf] != (ROOK | (us << 3))) return 0;
  int lo = kf, hi = kf;
  if (kt < lo) lo = kt;
  if (kt > hi) hi = kt;
  if (rf < lo) lo = rf;
  if (rf > hi) hi = rf;
  if (rt < lo) lo = rt;
  if (rt > hi) hi = rt;
  for (int s = lo; s <= hi; s++)
    if (s != kf && s != rf && p->b[s]) return 0;
  Pos t = *p;
  t.b[kf] = 0; t.b[rf] = 0;
  int a = kf < kt ? kf : kt, z = kf < kt ? kt : kf;
  for (int s = a; s <= z; s++)
    if (attacked(&t, s, us ^ 1)) return 0;
  return 1;
}

static int add_pawn(Move *ml, int n, int f, int t, int fl, int caps_only) {
  if (RANK_OF(t) == 0 || RANK_OF(t) == 7) {
    ml[n++] = MV(f, t, QUEEN, fl);
    if (!caps_only) {
      ml[n++] = MV(f, t, ROOK, fl);
      ml[n++] = MV(f, t, BISHOP, fl);
      ml[n++] = MV(f, t, KNIGHT, fl);
    }
  } else if (!caps_only || (fl & F_CAP)) {
    ml[n++] = MV(f, t, 0, fl);
  }
  return n;
}

int gen_moves(const Pos *p, Move *ml, int caps_only) {
  const uint8_t *b = p->b;
  int us = p->side, them = us ^ 1, n = 0;
  int up = us ? -16 : 16, start = us ? 6 : 1;
  for (int s = 0; s < 128; s++) {
    if (s & 0x88) { s += 7; continue; }
    int pc = b[s];
    if (!pc || PCOLOR(pc) != us) continue;
    int ty = PTYPE(pc), t;
    switch (ty) {
    case PAWN:
      t = s + up;
      if (!b[t]) {
        n = add_pawn(ml, n, s, t, 0, caps_only);
        if (!caps_only && RANK_OF(s) == start && !b[t + up]) ml[n++] = MV(s, t + up, 0, F_DPUSH);
      }
      for (int dc = -1; dc <= 1; dc += 2) {
        t = s + up + dc;
        if (!ONBOARD(t)) continue;
        if (b[t] && PCOLOR(b[t]) == them) n = add_pawn(ml, n, s, t, F_CAP, caps_only);
        else if (t == p->ep) ml[n++] = MV(s, t, 0, F_CAP | F_EP);
      }
      break;
    case KNIGHT:
    case KING:
      for (int i = 0; i < 8; i++) {
        t = s + (ty == KNIGHT ? NDIR[i] : KDIR[i]);
        if (!ONBOARD(t)) continue;
        if (!b[t]) { if (!caps_only) ml[n++] = MV(s, t, 0, 0); }
        else if (PCOLOR(b[t]) == them) ml[n++] = MV(s, t, 0, F_CAP);
      }
      if (ty == KING && !caps_only) {
        if (castle_ok(p, 0)) ml[n++] = MV(s, (us ? 0x70 : 0) | 6, 0, F_CASTLE);
        if (castle_ok(p, 1)) ml[n++] = MV(s, (us ? 0x70 : 0) | 2, 0, F_CASTLE | F_QSIDE);
      }
      break;
    default: {
      int i0 = (ty == BISHOP) ? 4 : 0, i1 = (ty == ROOK) ? 4 : 8;
      for (int i = i0; i < i1; i++) {
        int d = KDIR[i];
        for (t = s + d; ONBOARD(t); t += d) {
          if (!b[t]) { if (!caps_only) ml[n++] = MV(s, t, 0, 0); continue; }
          if (PCOLOR(b[t]) == them) ml[n++] = MV(s, t, 0, F_CAP);
          break;
        }
      }
    } }
  }
  return n;
}

int make_move(const Pos *p, Pos *n, Move m) {
  *n = *p;
  int us = p->side, them = us ^ 1;
  int from = MFROM(m), to = MTO(m), fl = MFLAGS(m);
  int pc = n->b[from];
  uint64_t k = n->key;
  if (n->ep != NOSQ) { k ^= zep[FILE_OF(n->ep)]; n->ep = NOSQ; }
  k ^= zcastle[n->castle];
  if (n->half < 255) n->half++;

  if (fl & F_CASTLE) {
    int r = us ? 0x70 : 0, qs = (fl & F_QSIDE) ? 1 : 0;
    int rf = r | c_rfile[qs], kt = r | (qs ? 2 : 6), rt = r | (qs ? 3 : 5);
    int rook = n->b[rf];
    n->b[from] = 0; k ^= ZP(pc, from);
    n->b[rf] = 0;   k ^= ZP(rook, rf);
    n->b[kt] = (uint8_t)pc;   k ^= ZP(pc, kt);
    n->b[rt] = (uint8_t)rook; k ^= ZP(rook, rt);
    n->ksq[us] = (uint8_t)kt;
    n->castle &= us ? ~12 : ~3;
  } else {
    if (fl & F_EP) {
      int cs = us == WHITE ? to - 16 : to + 16;
      k ^= ZP(n->b[cs], cs);
      n->b[cs] = 0;
      n->half = 0;
      n->men--;
    } else if (n->b[to]) {
      k ^= ZP(n->b[to], to);
      n->half = 0;
      n->men--;
    }
    int np = MPROMO(m) ? (MPROMO(m) | (us << 3)) : pc;
    n->b[from] = 0;           k ^= ZP(pc, from);
    n->b[to] = (uint8_t)np;   k ^= ZP(np, to);
    if (PTYPE(pc) == PAWN) {
      n->half = 0;
      if (fl & F_DPUSH) {
        int e = (from + to) >> 1;
        /* only record ep if an enemy pawn can actually capture */
        int ep_pawn = PAWN | (them << 3);
        if ((ONBOARD(to - 1) && n->b[to - 1] == ep_pawn) || (ONBOARD(to + 1) && n->b[to + 1] == ep_pawn)) {
          n->ep = (uint8_t)e; k ^= zep[FILE_OF(e)];
        }
      }
    }
    if (PTYPE(pc) == KING) n->ksq[us] = (uint8_t)to;
    n->castle &= castle_mask[from] & castle_mask[to];
  }
  k ^= zcastle[n->castle];
  n->side = (uint8_t)them;
  k ^= zside;
  if (us == BLACK) n->fullmove++;
  n->key = k;
  return !attacked(n, n->ksq[us], them);
}

int gen_legal(const Pos *p, Move *ml) {
  int n = gen_moves(p, ml, 0), k = 0;
  Pos q;
  for (int i = 0; i < n; i++)
    if (make_move(p, &q, ml[i])) ml[k++] = ml[i];
  return k;
}

uint64_t perft(const Pos *p, int depth) {
  Move ml[MAXMOVES];
  Pos q;
  int n = gen_moves(p, ml, 0);
  uint64_t c = 0;
  for (int i = 0; i < n; i++) {
    if (!make_move(p, &q, ml[i])) continue;
    c += depth > 1 ? perft(&q, depth - 1) : 1;
  }
  return c;
}

int castle_rook_sq(const Pos *p, Move m) {
  int r = p->side ? 0x70 : 0;
  return r | c_rfile[(MFLAGS(m) & F_QSIDE) ? 1 : 0];
}

/* ------------------------------------------------------------------ */
/*  Notation                                                          */
/* ------------------------------------------------------------------ */

void move_to_str(const Pos *p, Move m, char *s) {
  int from = MFROM(m), to = MTO(m);
  if (MFLAGS(m) & F_CASTLE) to = castle_rook_sq(p, m);
  s[0] = (char)('a' + FILE_OF(from)); s[1] = (char)('1' + RANK_OF(from));
  s[2] = (char)('a' + FILE_OF(to));   s[3] = (char)('1' + RANK_OF(to));
  s[4] = MPROMO(m) ? " pnbrqk"[MPROMO(m)] : 0;
  s[5] = 0;
}

Move str_to_move(const Pos *p, const char *s) {
  int n = gen_legal(p, scratch);
  char b[8];
  for (int i = 0; i < n; i++) {
    move_to_str(p, scratch[i], b);
    if (!strncmp(b, s, 5) && (b[4] || !s[4] || s[4] == ' ' || s[4] == '\n' || s[4] == '\r')) return scratch[i];
  }
  /* accept classic king-destination castling notation (e1g1) */
  for (int i = 0; i < n; i++) {
    Move m = scratch[i];
    if (!(MFLAGS(m) & F_CASTLE)) continue;
    int f = MFROM(m), t = MTO(m);
    if (s[0] == 'a' + FILE_OF(f) && s[1] == '1' + RANK_OF(f) &&
        s[2] == 'a' + FILE_OF(t) && s[3] == '1' + RANK_OF(t)) return m;
  }
  return 0;
}

void move_to_san(const Pos *p, Move m, char *s) {
  char *o = s;
  int fl = MFLAGS(m);
  if (fl & F_CASTLE) {
    strcpy(o, (fl & F_QSIDE) ? "O-O-O" : "O-O");
    o += strlen(o);
  } else {
    int from = MFROM(m), to = MTO(m), ty = PTYPE(p->b[from]);
    if (ty == PAWN) {
      if (fl & F_CAP) { *o++ = (char)('a' + FILE_OF(from)); *o++ = 'x'; }
    } else {
      *o++ = " PNBRQK"[ty];
      int n = gen_legal(p, scratch), same = 0, sf = 0, sr = 0;
      for (int i = 0; i < n; i++) {
        Move x = scratch[i];
        if (x == m || MTO(x) != to || (MFLAGS(x) & F_CASTLE)) continue;
        if (PTYPE(p->b[MFROM(x)]) != ty) continue;
        same = 1;
        if (FILE_OF(MFROM(x)) == FILE_OF(from)) sf = 1;
        if (RANK_OF(MFROM(x)) == RANK_OF(from)) sr = 1;
      }
      if (same) {
        if (!sf) *o++ = (char)('a' + FILE_OF(from));
        else if (!sr) *o++ = (char)('1' + RANK_OF(from));
        else { *o++ = (char)('a' + FILE_OF(from)); *o++ = (char)('1' + RANK_OF(from)); }
      }
      if (fl & F_CAP) *o++ = 'x';
    }
    *o++ = (char)('a' + FILE_OF(to));
    *o++ = (char)('1' + RANK_OF(to));
    if (MPROMO(m)) { *o++ = '='; *o++ = " PNBRQK"[MPROMO(m)]; }
  }
  Pos q;
  make_move(p, &q, m);
  if (in_check(&q)) *o++ = gen_legal(&q, scratch) ? '+' : '#';
  *o = 0;
}

int game_state(const Pos *p, const uint64_t *keys, int nkeys) {
  if (!gen_legal(p, scratch)) return in_check(p) ? 1 : 2;
  if (p->half >= 100) return 3;
  if (insufficient(p)) return 4;
  int cnt = 1;
  for (int i = nkeys - 3, k = 2; i >= 0 && k <= p->half; i -= 2, k += 2)
    if (keys[i] == p->key && ++cnt >= 3) return 5;
  return 0;
}

/* ------------------------------------------------------------------ */
/*  Evaluation                                                        */
/* ------------------------------------------------------------------ */

static const int16_t VAL[7] = { 0, 100, 320, 330, 500, 900, 0 };

/* Tables from white's point of view, a8 first (Michniewski) */
static const int8_t PST[7][64] = {
  { 0 },
  { 0,  0,  0,  0,  0,  0,  0,  0,
   50, 50, 50, 50, 50, 50, 50, 50,
   10, 10, 20, 30, 30, 20, 10, 10,
    5,  5, 10, 25, 25, 10,  5,  5,
    0,  0,  0, 20, 20,  0,  0,  0,
    5, -5,-10,  0,  0,-10, -5,  5,
    5, 10, 10,-20,-20, 10, 10,  5,
    0,  0,  0,  0,  0,  0,  0,  0 },
  {-50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50 },
  {-20,-10,-10,-10,-10,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -20,-10,-10,-10,-10,-10,-10,-20 },
  {  0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0 },
  {-20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
     0,  0,  5,  5,  5,  5,  0, -5,
   -10,  5,  5,  5,  5,  5,  0,-10,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20 },
  {-30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20 }
};
static const int8_t KING_EG[64] = {
  -50,-40,-30,-20,-20,-30,-40,-50,
  -30,-20,-10,  0,  0,-10,-20,-30,
  -30,-10, 20, 30, 30, 20,-10,-30,
  -30,-10, 30, 40, 40, 30,-10,-30,
  -30,-10, 30, 40, 40, 30,-10,-30,
  -30,-10, 20, 30, 30, 20,-10,-30,
  -30,-30,  0,  0,  0,  0,-30,-30,
  -50,-30,-30,-30,-30,-30,-30,-50 };
static const int8_t PASS_MG[8] = { 0, 5, 10, 15, 25, 40, 60, 0 };
static const uint8_t PASS_EG[8] = { 0, 10, 20, 35, 60, 100, 150, 0 };
static const int8_t PHASE[7] = { 0, 0, 1, 1, 2, 4, 0 };

/* the hand-written evaluation (without a network, or NO_NNUE) */
__attribute__((unused)) static int hce_evaluate(const Pos *p) {
  const uint8_t *b = p->b;
  int mg[2] = { 0, 0 }, eg[2] = { 0, 0 }, phase = 0, bish[2] = { 0, 0 };
  int8_t pcnt[2][10], pmin[2][10], pmax[2][10];
  memset(pcnt, 0, sizeof pcnt);
  memset(pmin, 8, sizeof pmin);
  memset(pmax, -1, sizeof pmax);
  uint8_t pl[32];                              /* the pawns and rooks, for the second pass */
  int npl = 0;

  for (int s = 0; s < 128; s++) {
    if (s & 0x88) { s += 7; continue; }
    int pc = b[s];
    if (!pc) continue;
    int c = PCOLOR(pc), ty = PTYPE(pc), f = FILE_OF(s), r = RANK_OF(s);
    if ((ty == PAWN || ty == ROOK) && npl < 32) pl[npl++] = (uint8_t)s;
    int idx = c ? (r * 8 + f) : ((7 - r) * 8 + f);
    phase += PHASE[ty];
    if (ty == KING) {
      mg[c] += PST[KING][idx];
      eg[c] += KING_EG[idx];
      continue;
    }
    mg[c] += VAL[ty] + PST[ty][idx];
    eg[c] += VAL[ty] + PST[ty][idx];
    if (ty == PAWN) {
      pcnt[c][f + 1]++;
      if (r < pmin[c][f + 1]) pmin[c][f + 1] = (int8_t)r;
      if (r > pmax[c][f + 1]) pmax[c][f + 1] = (int8_t)r;
    } else if (ty != KING) {
      if (ty == BISHOP) bish[c]++;
      /* mobility */
      int mob = 0;
      if (ty == KNIGHT) {
        for (int i = 0; i < 8; i++) { int t = s + NDIR[i]; if (ONBOARD(t) && (!b[t] || PCOLOR(b[t]) != c)) mob++; }
        mg[c] += 4 * (mob - 4); eg[c] += 4 * (mob - 4);
      } else {
        int i0 = ty == BISHOP ? 4 : 0, i1 = ty == ROOK ? 4 : 8;
        for (int i = i0; i < i1; i++)
          for (int t = s + KDIR[i]; ONBOARD(t); t += KDIR[i]) {
            if (!b[t]) { mob++; continue; }
            if (PCOLOR(b[t]) != c) mob++;
            break;
          }
        if (ty == BISHOP) { mg[c] += 5 * (mob - 6); eg[c] += 5 * (mob - 6); }
        else if (ty == ROOK) { mg[c] += 2 * (mob - 7); eg[c] += 4 * (mob - 7); }
        else { mg[c] += (mob - 14); eg[c] += 2 * (mob - 14); }
      }
    }
  }

  /* pawn structure + rooks on files */
  for (int i = 0; i < npl; i++) {
    int s = pl[i], pc = b[s];
    int c = PCOLOR(pc), ty = PTYPE(pc), f = FILE_OF(s) + 1, r = RANK_OF(s);
    if (ty == PAWN) {
      if (!pcnt[c][f - 1] && !pcnt[c][f + 1]) { mg[c] -= 10; eg[c] -= 15; }
      int passed;
      if (c == WHITE) passed = pmax[1][f - 1] <= r && pmax[1][f] <= r && pmax[1][f + 1] <= r;
      else            passed = pmin[0][f - 1] >= r && pmin[0][f] >= r && pmin[0][f + 1] >= r;
      if (passed) {
        int rr = c ? 7 - r : r;
        mg[c] += PASS_MG[rr];
        eg[c] += PASS_EG[rr];
      }
    } else if (ty == ROOK) {
      if (!pcnt[c][f]) {
        if (!pcnt[c ^ 1][f]) { mg[c] += 20; eg[c] += 10; }
        else { mg[c] += 10; eg[c] += 5; }
      }
    }
  }
  for (int c = 0; c < 2; c++) {
    for (int f = 1; f <= 8; f++) if (pcnt[c][f] > 1) { mg[c] -= 10 * (pcnt[c][f] - 1); eg[c] -= 20 * (pcnt[c][f] - 1); }
    if (bish[c] >= 2) { mg[c] += 30; eg[c] += 50; }

    /* opening/middlegame only: development, flank pawns, king pawn shield */
    int home = c ? 7 : 0, fwd = c ? -1 : 1;
    for (int f = 0; f < 8; f++) {
      int pc = b[SQ(f, home)];
      if (pc && PCOLOR(pc) == c && (PTYPE(pc) == KNIGHT || PTYPE(pc) == BISHOP)) mg[c] -= 12;
    }
    for (int f = 0; f < 8; f += 7) {           /* a- and h-pawns pushed early */
      int moved = pcnt[c][f + 1] && (c ? pmin[1][f + 1] < 5 : pmax[0][f + 1] > 2);
      if (moved) mg[c] -= 10;
    }
    int ks = p->ksq[c], kf = FILE_OF(ks), kr = RANK_OF(ks);
    if (kr == home) {
      int shield = 0;
      for (int f = kf - 1; f <= kf + 1; f++) {
        if (f < 0 || f > 7) continue;
        int a = b[SQ(f, kr + fwd)], a2 = b[SQ(f, kr + 2 * fwd)];
        if (a == (PAWN | (c << 3))) shield += 12;
        else if (a2 == (PAWN | (c << 3))) shield += 6;
      }
      if (kf >= 3 && kf <= 4) shield -= 10;    /* uncastled king in the centre */
      mg[c] += shield;
    }
  }
  if (phase > 24) phase = 24;
  int m = mg[0] - mg[1], e = eg[0] - eg[1];
  int sc = (m * phase + e * (24 - phase)) / 24;
  return (p->side == WHITE ? sc : -sc) + 10;
}

/* ------------------------------------------------------------------ */
/*  The evaluation network (src/nnue_chess.h, tools/nnue/train.py)    */
/* ------------------------------------------------------------------ */
/* Each side's view: 12 x 64 inputs (its own pieces first, the board turned over for Black), one layer of NNC_HID
   kept up to date (the "accumulator"), clipped to 0..QA, then one output. In the search each ply keeps its
   accumulators (nn_acc) with the key of the position they belong to: a position's come from the nearest ply below
   whose are current, by the squares that changed between the boards (castling, en passant, promotion alike). */
#ifdef HAVE_NNUE
static inline int nn_feat(int pc, int sq, int view) {
  int col = PCOLOR(pc), r = RANK_OF(sq);
  if (view) { col ^= 1; r = 7 - r; }
  return ((col * 6 + PTYPE(pc) - 1) * 64 + r * 8 + FILE_OF(sq)) * NNC_HID;
}
static void nn_refresh(const Pos *p, int16_t a[2][NNC_HID]) {
  for (int v = 0; v < 2; v++) {
    memcpy(a[v], NNC_B1, sizeof NNC_B1);
    for (int s = 0; s < 128; s++) {
      if (s & 0x88) { s += 7; continue; }
      if (!p->b[s]) continue;
      const int16_t *w = NNC_W1 + nn_feat(p->b[s], s, v);
      for (int i = 0; i < NNC_HID; i++) a[v][i] += w[i];
    }
  }
}
static void nn_update(const Pos *o, const Pos *n, int16_t src[2][NNC_HID], int16_t dst[2][NNC_HID]) {
  memcpy(dst, src, 2 * NNC_HID * sizeof(int16_t));
  const uint32_t *wo = (const uint32_t *)o->b, *wn = (const uint32_t *)n->b;
  for (int k = 0; k < 32; k++) {
    if (wo[k] == wn[k]) continue;
    for (int s = 4 * k; s < 4 * k + 4; s++) {
      int a = o->b[s], b = n->b[s];
      if (a == b) continue;
      for (int v = 0; v < 2; v++) {
        if (a) { const int16_t *w = NNC_W1 + nn_feat(a, s, v); for (int i = 0; i < NNC_HID; i++) dst[v][i] -= w[i]; }
        if (b) { const int16_t *w = NNC_W1 + nn_feat(b, s, v); for (int i = 0; i < NNC_HID; i++) dst[v][i] += w[i]; }
      }
    }
  }
}
static int nn_output(const int16_t a[2][NNC_HID], int side) {
  int32_t out = NNC_B2;
  const int16_t *us = a[side], *them = a[side ^ 1];
  for (int i = 0; i < NNC_HID; i++) {
    int x = us[i] < 0 ? 0 : us[i] > NNC_QA ? NNC_QA : us[i];
    int y = them[i] < 0 ? 0 : them[i] > NNC_QA ? NNC_QA : them[i];
    out += x * NNC_W2[i] + y * NNC_W2[NNC_HID + i];
  }
  return (int)((int64_t)out * NNC_SCALE / (NNC_QA * NNC_QB));
}
/* The endgames the network does not know well (4 men or fewer): king and pawn against king from the table
   (tools/kpk_gen.c: a draw is 0); a bare king against a queen, a rook or two bishops (driven to the edge, the kings
   close) and against bishop and knight (driven to a corner of the bishop's colour). 1 and *e (side to move's view)
   if the position is one of them. */
static int cheb64(int a, int b) {
  int df = (a & 7) - (b & 7), dr = (a >> 3) - (b >> 3);
  if (df < 0) df = -df;
  if (dr < 0) dr = -dr;
  return df > dr ? df : dr;
}
#define SQ64(s) (RANK_OF(s) * 8 + FILE_OF(s))
static int endgame(const Pos *p, int *e) {
  int pc[2], sq[2], n = 0;
  for (int s = 0; s < 120; s++) {
    if (s & 8) { s += 7; continue; }
    if (!p->b[s] || PTYPE(p->b[s]) == KING) continue;
    if (n == 2) return 0;
    pc[n] = p->b[s]; sq[n] = s; n++;
  }
  if (n == 1 && PTYPE(pc[0]) == PAWN) {
    int c = PCOLOR(pc[0]);
    int wk = SQ64(p->ksq[c]), bk = SQ64(p->ksq[c ^ 1]), ps = SQ64(sq[0]), stm = p->side != c;
    if (c == BLACK) { wk ^= 56; bk ^= 56; ps ^= 56; }
    if ((ps & 7) > 3) { wk ^= 7; bk ^= 7; ps ^= 7; }
    int i = ((stm * 24 + ((ps >> 3) - 1) * 4 + (ps & 7)) * 64 + wk) * 64 + bk;
    int v = (KPK[i >> 3] >> (i & 7)) & 1 ? 600 + 30 * (ps >> 3) : 0;
    *e = p->side == c ? v : -v;
    return 1;
  }
  if (n == 2 && PCOLOR(pc[0]) != PCOLOR(pc[1])) return 0;
  int c = n ? PCOLOR(pc[0]) : 0, t0 = n ? PTYPE(pc[0]) : 0, t1 = n == 2 ? PTYPE(pc[1]) : 0;
  if (!t1 ? t0 != QUEEN && t0 != ROOK : t0 == KNIGHT && t1 == KNIGHT) {
    *e = 0;                                    /* no mate can be forced: king and one minor piece, or two knights */
    return 1;
  }
  int w = SQ64(p->ksq[c ^ 1]), kd = cheb64(SQ64(p->ksq[c]), w);
  if (t0 == QUEEN || t0 == ROOK || t1 == QUEEN || t1 == ROOK || (t0 == BISHOP && t1 == BISHOP)) {
    if (t0 == PAWN || t1 == PAWN) return 0;
    if (t0 == BISHOP && ((FILE_OF(sq[0]) + RANK_OF(sq[0]) + FILE_OF(sq[1]) + RANK_OF(sq[1])) & 1) == 0) {
      *e = 0;                                                       /* two bishops on one colour: no mate */
      return 1;
    }
    int f = w & 7, r = w >> 3, cf = f < 4 ? 3 - f : f - 4, cr = r < 4 ? 3 - r : r - 4;
    static const int16_t PV[7] = { 0, 100, 320, 330, 500, 900, 0 };
    int v = 500 + PV[t0] + PV[t1] + 50 * (cf > cr ? cf : cr) + 10 * (7 - kd);
    *e = p->side == c ? v : -v;
    return 1;
  }
  if (n == 2 && t0 + t1 == KNIGHT + BISHOP && t0 != t1) {
    int bs = SQ64(t0 == BISHOP ? sq[0] : sq[1]), dark = !(((bs & 7) + (bs >> 3)) & 1);       /* a1 is dark */
    int d1 = (w & 7) + (w >> 3), d2 = 14 - d1;                              /* to a1 / h8 */
    if (!dark) { d1 = (7 - (w & 7)) + (w >> 3); d2 = 14 - d1; }             /* to h1 / a8 */
    int ns = SQ64(t0 == KNIGHT ? sq[0] : sq[1]);
    int v = 1000 + 30 * (14 - (d1 < d2 ? d1 : d2)) + 10 * (7 - kd) - 3 * cheb64(ns, w);
    *e = p->side == c ? v : -v;
    return 1;
  }
  return 0;
}
int evaluate(const Pos *p) {
  if (eval_hand) return hce_evaluate(p);                     /* levels 1-3 */
  if (p->men <= 4) { int e; if (endgame(p, &e)) return e; }
  uintptr_t d = (uintptr_t)p - (uintptr_t)stk;                /* unsigned: a position elsewhere is far out */
  int ply = (int)(d / sizeof(Pos));
  if (!stk || d >= (MAXPLY + 2) * sizeof(Pos) || d % sizeof(Pos)) {   /* outside the search: from the board */
    int16_t a[2][NNC_HID];
    nn_refresh(p, a);
    return nn_output(a, p->side);
  }
  if (nn_key[ply] != p->key) {
    int k = ply - 1;
    while (k >= 0 && nn_key[k] != stk[k].key) k--;
    if (k < 0) { nn_refresh(&stk[0], nn_acc[0]); nn_key[0] = stk[0].key; k = 0; }
    for (; k < ply; k++) { nn_update(&stk[k], &stk[k + 1], nn_acc[k], nn_acc[k + 1]); nn_key[k + 1] = stk[k + 1].key; }
  }
#ifdef NN_CHECK
  { int16_t t[2][NNC_HID]; nn_refresh(p, t);                 /* the update must give the recount's numbers */
    if (memcmp(t, nn_acc[ply], sizeof t)) { extern void nn_check_failed(void); nn_check_failed(); } }
#endif
  return nn_output(nn_acc[ply], p->side);
}
#else
int evaluate(const Pos *p) { return hce_evaluate(p); }
#endif

/* ------------------------------------------------------------------ */
/*  Search                                                            */
/* ------------------------------------------------------------------ */

static uint32_t nodes;
/* ------------------------------------------------------------------ */
/*  Search: the shared src/search.inc with chess's hooks               */
/* ------------------------------------------------------------------ */

static int has_pieces(const Pos *p) {
  int c = p->side;
  for (int s = 0; s < 128; s++) {
    if (s & 0x88) { s += 7; continue; }
    int pc = p->b[s];
    if (pc && PCOLOR(pc) == c && PTYPE(pc) != PAWN && PTYPE(pc) != KING) return 1;
  }
  return 0;
}

static inline int32_t chess_tactical(const Pos *p, Move m) {
  if (MFLAGS(m) & F_CAP) {
    int vic = (MFLAGS(m) & F_EP) ? PAWN : PTYPE(p->b[MTO(m)]);
    return 1000000 + vic * 100 - PTYPE(p->b[MFROM(m)]);
  }
  return MPROMO(m) == QUEEN ? 950000 : -1;
}

static inline int chess_qprune(const Pos *p, Move m, int stand, int alpha, int margin) {
  if (MPROMO(m)) return 0;
  int vic = (MFLAGS(m) & F_EP) ? PAWN : PTYPE(p->b[MTO(m)]);
  return stand + VAL[vic] + margin < alpha;
}

static inline int chess_badcap(const Pos *p, Move m) {
  if ((MFLAGS(m) & F_EP) || MPROMO(m)) return 0;
  int att = PTYPE(p->b[MFROM(m)]), vic = PTYPE(p->b[MTO(m)]);
  if (att == KING) return 0;                  /* a king's capture is legal only if the point is not defended */
  return VAL[att] - VAL[vic] > 50 && attacked(p, MTO(m), p->side ^ 1);
}

static inline void chess_null(const Pos *p, Pos *n) {
  *n = *p;
  n->side ^= 1;
  n->key ^= zside;
  if (n->ep != NOSQ) { n->key ^= zep[FILE_OF(n->ep)]; n->ep = NOSQ; }
  n->half = 0;
}

const EngShared *eng_shared(void) {
  static EngShared sh;
  sh.tt = tt; sh.tt_n = tt_n; sh.mbuf = mbuf; sh.sbuf = sbuf; sh.killer = killer; sh.rep = rep; sh.scratch = scratch; sh.pvh = pvh; sh.hh = hh;
  return &sh;
}

#define SEARCH_NAME            search
#define G_TACTICAL(p, m)       chess_tactical(p, m)
#define G_ORDER_ADJ(m)         ((MPROMO(m) && MPROMO(m) != QUEEN) ? -2000000 : 0)   /* under-promotions last */
#define G_HIDX(p, m)           (((p)->b[MFROM(m)] << 7) | MTO(m))
#define G_HSIZE                (16 * 128)
#define G_QPRUNE(p, m, st, a)  chess_qprune(p, m, st, a, SP_DELTA)
#define G_DRAWN(p)             ((p)->half >= 100)
#define G_HAS_PIECES(p)        has_pieces(p)
#define G_NULL_MOVE(p, n)      chess_null(p, n)
#define G_QUIET(m)             (!(MFLAGS(m) & F_CAP) && !MPROMO(m))
#define G_NOLEGAL(chk, ply)    ((chk) ? -MATE + (ply) : 0)
#define G_BADCAP(p, m)         chess_badcap(p, m)

#define SP_TT2                 1     /* the hash table in buckets of two (Xiangqi: no gain) */
#define PV_NAME search_pv
#include "search.inc"
