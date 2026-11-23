/*
 * CHESS47 for DM42n (DMCP5) - the Xiangqi (Chinese chess) engine
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 *
 * The rules: the general (K) and the advisors (A) stay in the palace (files d-f); the elephants (E, "B" in FEN)
 * move two diagonally, blocked at the eye, and never cross the river; the horse (H, "N" in FEN) moves like a
 * knight but is blocked by a piece next to it in the long direction (the leg); the chariot (R) moves like a rook;
 * the cannon (C) moves like a rook and captures by jumping exactly one piece (the screen); the soldier (P) moves one
 * forward, and also sideways once over the river. The generals may never face each other on an open file. No legal
 * move loses (stalemate too). Repetition is a draw here (the real perpetual-check and chase rules are not modelled).
 *
 * Memory: the hash table and the move stacks are chess's (eng_shared(): one game plays at a time); here only the
 * Zobrist keys (14 x 90), the position stack and the history table, on the heap.
 */
#include "xq_engine.h"
#include <string.h>

/* the evaluation network, when tools/nnue/train.py has made one (NO_NNUE: the hand-written evaluation) */
#if !defined(NO_NNUE) && __has_include("nnue_xq.h")
#include "nnue_xq.h"
#define HAVE_NNUE 1
static int16_t (*nn_acc)[2][NNX_HID];        /* [MAXPLY + 2] the accumulators of the search's plies */
static uint64_t *nn_key;                     /* [MAXPLY + 2] the position each belongs to */
#endif
static int eval_hand;                        /* this search uses the hand-written evaluation (Limits.hand_eval) */

typedef XPos Pos;                          /* for src/search.inc */

typedef struct { uint64_t key; Move move; int16_t score; int8_t depth; uint8_t flag; } TTE;   /* as chess's */
#define TT_LOWER 1
#define TT_UPPER 2
#define TT_EXACT 3
#define MBUF (MAXPLY * 96)

static TTE     *tt;
static uint32_t tt_n;
static Pos     *stk;
static Move    *mbuf;
static int32_t *sbuf;
static uint16_t mofs[MAXPLY + 2];
static Move   (*killer)[2];
static int16_t *hh;                        /* history [16 * 256]: chess's (eng_shared) */
static uint64_t *rep;
static int      rep_n;
static Move    *scratch;
static PVE     *pvh;
static uint32_t nodes;
static uint64_t *zp, zside;                /* zp [14 * 90] */

static const int8_t ORTH[4] = { 1, -1, 16, -16 };
static const int8_t DIAG[4] = { 17, 15, -15, -17 };
static const int8_t HD[8]   = { 33, 31, -31, -33, 18, 14, -14, -18 };   /* horse: target */
static const int8_t HL[8]   = { 16, 16, -16, -16, 1, -1, 1, -1 };        /* its leg */

#define ZI(pc, s) zp[((XTYPE(pc) - 1) + 7 * XCOLOR(pc)) * 90 + XRANK(s) * 9 + XFILE(s)]

static int in_palace(int c, int s) {
  int f = XFILE(s), r = XRANK(s);
  return f >= 3 && f <= 5 && (c == XRED ? r >= 0 && r <= 2 : r >= 7 && r <= 9);
}
static int own_half(int c, int s) { int r = XRANK(s); return c == XRED ? r <= 4 : r >= 5; }

static uint64_t rs = 0x2545F4914F6CDD1DULL;
static uint64_t rnd64(void) { uint64_t x = rs; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return rs = x; }

int xq_init(void *(*alloc)(size_t)) {
  const EngShared *sh = eng_shared();
  tt = (TTE *)sh->tt; tt_n = sh->tt_n;
  mbuf = sh->mbuf; sbuf = sh->sbuf; killer = sh->killer; rep = sh->rep; scratch = sh->scratch; pvh = sh->pvh; hh = sh->hh;
  if (!tt || !mbuf) return 0;
  if (!zp) {
    zp = alloc(14 * 90 * sizeof(uint64_t));
    stk = alloc((MAXPLY + 2) * sizeof(Pos));
    if (!zp || !stk) return 0;
#ifdef HAVE_NNUE
    nn_acc = alloc((MAXPLY + 2) * sizeof *nn_acc);
    nn_key = alloc((MAXPLY + 2) * sizeof(uint64_t));
    if (!nn_acc || !nn_key) return 0;
#endif
    for (int i = 0; i < 14 * 90; i++) zp[i] = rnd64();
    zside = rnd64();
  }
  memset(hh, 0, HH_N * sizeof(int16_t));
  return 1;
}

static uint64_t compute_key(const Pos *p) {
  uint64_t k = p->side ? zside : 0;
  for (int r = 0; r < 10; r++)
    for (int f = 0; f < 9; f++) { int s = XSQ(f, r); if (p->b[s]) k ^= ZI(p->b[s], s); }
  return k;
}

/* ------------------------------------------------------------------ */
/*  Positions                                                         */
/* ------------------------------------------------------------------ */

static char *put_int(char *o, int v) {
  char t[12]; int n = 0;
  if (v < 0) { *o++ = '-'; v = -v; }
  do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n) *o++ = t[--n];
  return o;
}

static const char LET[8] = { 0, 'K', 'A', 'B', 'N', 'R', 'C', 'P' };

static int piece_of(char ch) {
  int black = ch >= 'a' && ch <= 'z';
  char u = black ? (char)(ch - 32) : ch;
  int t = u == 'K' ? XKING : u == 'A' ? XADVISOR : (u == 'B' || u == 'E') ? XELEPHANT :
          (u == 'N' || u == 'H') ? XHORSE : u == 'R' ? XCHARIOT : u == 'C' ? XCANNON : u == 'P' ? XPAWN : 0;
  return t ? t | (black << 3) : 0;
}

int xq_from_fen(Pos *p, const char *f) {
  memset(p, XOFF, sizeof p->b);
  for (int r = 0; r < 10; r++) for (int c = 0; c < 9; c++) p->b[XSQ(c, r)] = 0;
  p->ksq[0] = p->ksq[1] = 0;
  int r = 9, c = 0;
  for (; *f && *f != ' '; f++) {
    if (*f == '/') { r--; c = 0; continue; }
    if (*f >= '1' && *f <= '9') { c += *f - '0'; continue; }
    int pc = piece_of(*f);
    if (!pc || r < 0 || c > 8) return 0;
    p->b[XSQ(c, r)] = (uint8_t)pc;
    if (XTYPE(pc) == XKING) p->ksq[XCOLOR(pc)] = (uint8_t)XSQ(c, r);
    c++;
  }
  if (!p->ksq[0] || !p->ksq[1]) return 0;
  while (*f == ' ') f++;
  p->side = (*f == 'b') ? XBLACK : XRED;
  p->half = 0; p->fullmove = 1;
  /* "- - half full" */
  int fields = 0;
  while (*f) {
    while (*f && *f != ' ') f++;
    while (*f == ' ') f++;
    if (!*f) break;
    fields++;
    if (fields >= 3 && *f >= '0' && *f <= '9') {
      int v = 0; while (*f >= '0' && *f <= '9') v = v * 10 + (*f++ - '0');
      if (fields == 3) p->half = (uint8_t)(v > 255 ? 255 : v); else p->fullmove = (uint16_t)v;
    }
  }
  p->key = compute_key(p);
  return 1;
}

void xq_to_fen(const Pos *p, char *o) {
  for (int r = 9; r >= 0; r--) {
    int e = 0;
    for (int c = 0; c < 9; c++) {
      int pc = p->b[XSQ(c, r)];
      if (!pc) { e++; continue; }
      if (e) { *o++ = (char)('0' + e); e = 0; }
      char ch = LET[XTYPE(pc)];
      *o++ = XCOLOR(pc) ? (char)(ch + 32) : ch;
    }
    if (e) *o++ = (char)('0' + e);
    if (r) *o++ = '/';
  }
  *o++ = ' '; *o++ = p->side ? 'b' : 'w';
  *o++ = ' '; *o++ = '-'; *o++ = ' '; *o++ = '-'; *o++ = ' ';
  o = put_int(o, p->half); *o++ = ' ';
  o = put_int(o, p->fullmove); *o = 0;
}

void xq_pos_start(Pos *p) { xq_from_fen(p, "rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1"); }

/* ------------------------------------------------------------------ */
/*  Attacks and moves                                                 */
/* ------------------------------------------------------------------ */

/* by attacks sq: a chariot or (on the file) the general on the ray; a cannon behind exactly one screen; a horse
   with a free leg; a soldier in front, or beside once over the river. */
int xq_attacked(const Pos *p, int sq, int by) {
  const uint8_t *b = p->b;
  for (int k = 0; k < 4; k++) {
    int d = ORTH[k], t = sq + d;
    while (!b[t]) t += d;
    if (b[t] == XOFF) continue;
    int q = b[t];
    if (XCOLOR(q) == by && (XTYPE(q) == XCHARIOT || (XTYPE(q) == XKING && (d == 16 || d == -16)))) return 1;
    t += d;
    while (!b[t]) t += d;
    if (b[t] != XOFF && b[t] == (XCANNON | by << 3)) return 1;
  }
  int hz = XHORSE | by << 3;
  for (int k = 0; k < 8; k++) {
    int h = sq - HD[k];
    if (b[h] == hz && !b[h + HL[k]]) return 1;
  }
  int pz = XPAWN | by << 3, fw = by == XRED ? 16 : -16;
  if (b[sq - fw] == pz) return 1;
  if (b[sq - 1] == pz && !own_half(by, sq - 1)) return 1;
  if (b[sq + 1] == pz && !own_half(by, sq + 1)) return 1;
  return 0;
}

int xq_in_check(const Pos *p) { return xq_attacked(p, p->ksq[p->side], p->side ^ 1); }

static int add(const Pos *p, Move *ml, int n, int f, int t, int caps_only) {
  int q = p->b[t];
  if (q == XOFF) return n;
  if (q) { if (XCOLOR(q) != p->side) ml[n++] = XMV(f, t, XF_CAP); }
  else if (!caps_only) ml[n++] = XMV(f, t, 0);
  return n;
}

int xq_gen_moves(const Pos *p, Move *ml, int caps_only) {
  const uint8_t *b = p->b;
  int n = 0, c = p->side;
  for (int r = 0; r < 10; r++)
    for (int fl = 0; fl < 9; fl++) {
      int s = XSQ(fl, r), pc = b[s];
      if (!pc || XCOLOR(pc) != c) continue;
      switch (XTYPE(pc)) {
      case XKING:
        for (int k = 0; k < 4; k++) if (in_palace(c, s + ORTH[k])) n = add(p, ml, n, s, s + ORTH[k], caps_only);
        break;
      case XADVISOR:
        for (int k = 0; k < 4; k++) if (in_palace(c, s + DIAG[k])) n = add(p, ml, n, s, s + DIAG[k], caps_only);
        break;
      case XELEPHANT:
        for (int k = 0; k < 4; k++) {
          int t = s + 2 * DIAG[k];
          if (b[t] != XOFF && own_half(c, t) && !b[s + DIAG[k]]) n = add(p, ml, n, s, t, caps_only);
        }
        break;
      case XHORSE:
        for (int k = 0; k < 8; k++) if (!b[s + HL[k]]) n = add(p, ml, n, s, s + HD[k], caps_only);
        break;
      case XCHARIOT:
        for (int k = 0; k < 4; k++) {
          int d = ORTH[k], t = s + d;
          while (!b[t]) { if (!caps_only) ml[n++] = XMV(s, t, 0); t += d; }
          if (b[t] != XOFF && XCOLOR(b[t]) != c) ml[n++] = XMV(s, t, XF_CAP);
        }
        break;
      case XCANNON:
        for (int k = 0; k < 4; k++) {
          int d = ORTH[k], t = s + d;
          while (!b[t]) { if (!caps_only) ml[n++] = XMV(s, t, 0); t += d; }
          if (b[t] == XOFF) continue;
          t += d;
          while (!b[t]) t += d;
          if (b[t] != XOFF && XCOLOR(b[t]) != c) ml[n++] = XMV(s, t, XF_CAP);
        }
        break;
      case XPAWN:
        n = add(p, ml, n, s, s + (c == XRED ? 16 : -16), caps_only);
        if (!own_half(c, s)) { n = add(p, ml, n, s, s + 1, caps_only); n = add(p, ml, n, s, s - 1, caps_only); }
        break;
      }
    }
  return n;
}

int xq_make_move(const Pos *p, Pos *n, Move m) {
  *n = *p;
  int f = XFROM(m), t = XTO(m), pc = n->b[f], cap = n->b[t];
  n->key ^= ZI(pc, f) ^ ZI(pc, t) ^ zside;
  if (cap) n->key ^= ZI(cap, t);
  n->b[t] = (uint8_t)pc;
  n->b[f] = 0;
  if (XTYPE(pc) == XKING) n->ksq[p->side] = (uint8_t)t;
  n->half = cap ? 0 : (uint8_t)(n->half < 255 ? n->half + 1 : 255);
  if (p->side == XBLACK) n->fullmove++;
  n->side ^= 1;
  return !xq_attacked(n, n->ksq[p->side], n->side);   /* the own general attacked, or facing the other */
}

int xq_gen_legal(const Pos *p, Move *ml) {
  Move tmp[MAXMOVES];
  Pos c;
  int n = xq_gen_moves(p, tmp, 0), k = 0;
  for (int i = 0; i < n; i++) if (xq_make_move(p, &c, tmp[i])) ml[k++] = tmp[i];
  return k;
}

uint64_t xq_perft(const Pos *p, int depth) {
  Move ml[MAXMOVES];
  Pos c;
  int n = xq_gen_moves(p, ml, 0);
  uint64_t t = 0;
  for (int i = 0; i < n; i++) {
    if (!xq_make_move(p, &c, ml[i])) continue;
    t += depth > 1 ? xq_perft(&c, depth - 1) : 1;
  }
  return t;
}

/* ------------------------------------------------------------------ */
/*  Notation                                                          */
/* ------------------------------------------------------------------ */

void xq_move_to_str(Move m, char *s, int rank0) {
  int f = XFROM(m), t = XTO(m), add1 = rank0 ? 0 : 1;
  *s++ = (char)('a' + XFILE(f)); s = put_int(s, XRANK(f) + add1);
  *s++ = (char)('a' + XFILE(t)); s = put_int(s, XRANK(t) + add1);
  *s = 0;
}

static Move parse_with(const Pos *p, const char *s, int add1) {
  int sq[2];
  for (int k = 0; k < 2; k++) {
    if (*s < 'a' || *s > 'i') return 0;
    int f = *s++ - 'a';
    if (*s < '0' || *s > '9') return 0;
    int r = 0; while (*s >= '0' && *s <= '9') r = r * 10 + (*s++ - '0');
    r -= add1;
    if (r < 0 || r > 9) return 0;
    sq[k] = XSQ(f, r);
  }
  Move ml[MAXMOVES];
  int n = xq_gen_legal(p, ml);
  for (int i = 0; i < n; i++) if (XFROM(ml[i]) == sq[0] && XTO(ml[i]) == sq[1]) return ml[i];
  return 0;
}

Move xq_str_to_move(const Pos *p, const char *s) {
  Move m = parse_with(p, s, 0);                  /* ranks 0-9 (Pikafish) */
  return m ? m : parse_with(p, s, 1);            /* ranks 1-10 (Fairy-Stockfish) */
}

/* WXF: the piece, its file counted from the mover's right (1-9; with two of them on the file + front, - rear),
   + forward, - back, = sideways, then the ranks moved (K R C P straight) or the file reached. */
void xq_move_to_wxf(const Pos *p, Move m, char *s) {
  static const char W[8] = { 0, 'K', 'A', 'E', 'H', 'R', 'C', 'P' };
  int f = XFROM(m), t = XTO(m), pc = p->b[f], c = XCOLOR(pc), ty = XTYPE(pc);
  int ff = XFILE(f), rf = XRANK(f), ft = XFILE(t), rt = XRANK(t);
  int fnum = c == XRED ? 9 - ff : ff + 1, tnum = c == XRED ? 9 - ft : ft + 1;
  *s++ = W[ty];
  int front = 0, other = 0;
  for (int r = 0; r < 10; r++) {
    if (r == rf || p->b[XSQ(ff, r)] != pc) continue;
    other = 1;
    if (c == XRED ? r > rf : r < rf) front = 1;     /* another one ahead: this is the rear one */
  }
  /* two on the file: front / rear, but not the advisors and elephants (their diagonal move tells them apart) */
  if (other && ty != XADVISOR && ty != XELEPHANT) *s++ = front ? '-' : '+'; else s = put_int(s, fnum);
  int fwd = c == XRED ? rt - rf : rf - rt;
  *s++ = fwd > 0 ? '+' : fwd < 0 ? '-' : '=';
  if (ty == XADVISOR || ty == XELEPHANT || ty == XHORSE || fwd == 0) s = put_int(s, tnum);
  else s = put_int(s, fwd > 0 ? fwd : -fwd);
  *s = 0;
}

/* ------------------------------------------------------------------ */
/*  Game state, evaluation                                            */
/* ------------------------------------------------------------------ */

int xq_game_state(const Pos *p, const uint64_t *keys, int nkeys) {
  Move ml[MAXMOVES];
  if (!xq_gen_legal(p, ml)) return xq_in_check(p) ? 1 : 2;
  if (p->half >= 120) return 3;
  int att = 0;
  for (int r = 0; r < 10 && !att; r++)
    for (int f = 0; f < 9; f++) {
      int ty = XTYPE(p->b[XSQ(f, r)]);
      if (ty == XHORSE || ty == XCHARIOT || ty == XCANNON || ty == XPAWN) { att = 1; break; }
    }
  if (!att) return 4;
  int same = 0;
  for (int i = nkeys - 1, k = 0; i >= 0 && k <= p->half; i--, k++) if (keys[i] == p->key) same++;
  return same >= 3 ? 5 : 0;
}

static const int16_t XVAL[8] = { 0, 0, 200, 200, 400, 900, 450, 100 };

/* bonuses from the owner's side: rr its rank counted from its own back rank, f the file */
static int pst(int ty, int rr, int f) {
  int cen = 4 - (f > 4 ? f - 4 : 4 - f);           /* 0 at the edge, 4 on file e */
  switch (ty) {
  case XPAWN:
    if (rr <= 4) return rr == 4 ? 10 : 0;
    return 90 + (rr <= 7 ? 10 * cen : rr == 8 ? 5 * cen : 0) + (rr == 6 || rr == 7 ? 20 : 0);
  case XHORSE:   return 8 * cen + (rr >= 3 && rr <= 7 ? 15 : 0) - (rr == 0 ? 15 : 0);
  case XCANNON:  return (f == 4 ? 15 : 0) + (rr == 2 || rr == 7 ? 5 : 0);
  case XCHARIOT: return (rr >= 5 ? 10 : 0) + 2 * cen;
  case XKING:    return f == 4 ? 10 : 0;
  default:       return 0;
  }
}

#ifndef XE_MOB_R
#define XE_MOB_R 3       /* a chariot's reachable point */
#endif
#ifndef XE_MOB_H
#define XE_MOB_H 10      /* a horse's free jump (a blocked leg costs it) */
#endif
#ifndef XE_MOB_C
#define XE_MOB_C 2       /* a cannon's point */
#endif
#ifndef XE_PHASE
#define XE_PHASE 6       /* per major piece on the board above 6: the cannon gains it, the horse loses it */
#endif
#ifndef XE_ATT
#define XE_ATT 14        /* the attack on a general: units * units * XE_ATT / 4, more with its guards gone */
#endif
#ifndef XE_CCN
#define XE_CCN 25        /* a cannon on the general's file with one screen (it pins, threatens) */
#endif

/* the hand-written evaluation (without a network, or NO_NNUE) */
__attribute__((unused)) static int hce_evaluate(const Pos *p) {
  const uint8_t *b = p->b;
  int sc[2] = { 0, 0 }, guard[2] = { 0, 0 }, units[2] = { 0, 0 }, maj = 0, ch[2] = { 0, 0 };
  int gf[2] = { XFILE(p->ksq[0]), XFILE(p->ksq[1]) }, gr[2] = { XRANK(p->ksq[0]), XRANK(p->ksq[1]) };
  for (int r = 0; r < 10; r++)
    for (int f = 0; f < 9; f++) {
      int s = XSQ(f, r), pc = b[s];
      if (!pc) continue;
      int c = XCOLOR(pc), ty = XTYPE(pc), rr = c == XRED ? r : 9 - r, o = c ^ 1;
      int df = f > gf[o] ? f - gf[o] : gf[o] - f, dr = r > gr[o] ? r - gr[o] : gr[o] - r;   /* to the other general */
      sc[c] += XVAL[ty] + pst(ty, rr, f);
      switch (ty) {
      case XADVISOR: case XELEPHANT: guard[c]++; break;
      case XPAWN: if (df + dr <= 2) units[c] += 2; break;
      case XCHARIOT: {
        int n = 0;
        for (int k = 0; k < 4; k++) {
          int d = ORTH[k], t = s + d;
          while (!b[t]) { n++; t += d; }
          if (b[t] != XOFF && XCOLOR(b[t]) != c) n++;
        }
        sc[c] += n * XE_MOB_R;
        if (df <= 1 || dr <= 1) units[c] += 3;
        maj++;
        break;
      }
      case XHORSE: {
        int n = 0;
        for (int k = 0; k < 8; k++) {
          int t = s + HD[k];
          if (!b[s + HL[k]] && b[t] != XOFF && (!b[t] || XCOLOR(b[t]) != c)) n++;
        }
        sc[c] += n * XE_MOB_H - 16;                     /* a horse with two jumps or fewer is a poor horse */
        if (df + dr <= 4) units[c] += 2;
        maj++; ch[c]--;
        break;
      }
      case XCANNON: {
        int n = 0;
        for (int k = 0; k < 4; k++) {
          int d = ORTH[k], t = s + d;
          while (!b[t]) { n++; t += d; }
          if (b[t] == XOFF) continue;
          t += d;
          while (!b[t]) t += d;
          if (b[t] != XOFF && XCOLOR(b[t]) != c) n++;
        }
        sc[c] += n * XE_MOB_C;
        if (df == 0 || dr == 0) units[c] += 2;
        if (df == 0) {                                  /* on the other general's file, one piece between */
          int g = p->ksq[o], d = gr[o] > r ? 16 : -16, t = s + d, between = 0;
          while (t != g) { if (b[t]) between++; t += d; }
          if (between == 1) sc[c] += XE_CCN;
        }
        maj++; ch[c]++;
        break;
      }
      }
    }
  /* the cannon is worth more with many pieces (screens), the horse in the endgame (open points) */
  int adj = (maj - 6) * XE_PHASE;
  sc[0] += ch[0] * adj;
  sc[1] += ch[1] * adj;
  /* a general without its guards is easier to attack: the attackers close to it count more */
  sc[0] -= (4 - guard[0]) * 15;
  sc[1] -= (4 - guard[1]) * 15;
  sc[0] += units[0] * units[0] * XE_ATT / 4 * (8 - guard[1]) / 6;
  sc[1] += units[1] * units[1] * XE_ATT / 4 * (8 - guard[0]) / 6;
  int v = sc[0] - sc[1];
  return (p->side == XRED ? v : -v) + 10;
}

/* ------------------------------------------------------------------ */
/*  The evaluation network (src/nnue_xq.h): as chess's (src/engine.c)  */
/* ------------------------------------------------------------------ */
/* Each side's view: 14 x 90 inputs (its own pieces first, the board turned over for Black), NNX_HID kept up to date
   in the search by the points that changed between the boards, clipped to 0..QA, then one output. */
#ifdef HAVE_NNUE
static inline int nn_feat(int pc, int sq, int view) {
  int col = XCOLOR(pc), r = XRANK(sq);
  if (view) { col ^= 1; r = 9 - r; }
  return ((col * 7 + XTYPE(pc) - 1) * 90 + r * 9 + XFILE(sq)) * NNX_HID;
}
static void nn_refresh(const Pos *p, int16_t a[2][NNX_HID]) {
  for (int v = 0; v < 2; v++) {
    memcpy(a[v], NNX_B1, sizeof NNX_B1);
    for (int r = 0; r < 10; r++)
      for (int f = 0; f < 9; f++) {
        int s = XSQ(f, r);
        if (!p->b[s]) continue;
        const int16_t *w = NNX_W1 + nn_feat(p->b[s], s, v);
        for (int i = 0; i < NNX_HID; i++) a[v][i] += w[i];
      }
  }
}
static void nn_update(const Pos *o, const Pos *n, int16_t src[2][NNX_HID], int16_t dst[2][NNX_HID]) {
  memcpy(dst, src, 2 * NNX_HID * sizeof(int16_t));
  const uint32_t *wo = (const uint32_t *)o->b, *wn = (const uint32_t *)n->b;
  for (int k = 0; k < 64; k++) {
    if (wo[k] == wn[k]) continue;
    for (int s = 4 * k; s < 4 * k + 4; s++) {
      int a = o->b[s], b = n->b[s];
      if (a == b) continue;
      for (int v = 0; v < 2; v++) {
        if (a) { const int16_t *w = NNX_W1 + nn_feat(a, s, v); for (int i = 0; i < NNX_HID; i++) dst[v][i] -= w[i]; }
        if (b) { const int16_t *w = NNX_W1 + nn_feat(b, s, v); for (int i = 0; i < NNX_HID; i++) dst[v][i] += w[i]; }
      }
    }
  }
}
static int nn_output(const int16_t a[2][NNX_HID], int side) {
  int32_t out = NNX_B2;
  const int16_t *us = a[side], *them = a[side ^ 1];
  for (int i = 0; i < NNX_HID; i++) {
    int x = us[i] < 0 ? 0 : us[i] > NNX_QA ? NNX_QA : us[i];
    int y = them[i] < 0 ? 0 : them[i] > NNX_QA ? NNX_QA : them[i];
    out += x * NNX_W2[i] + y * NNX_W2[NNX_HID + i];
  }
  return (int)((int64_t)out * NNX_SCALE / (NNX_QA * NNX_QB));
}
int xq_evaluate(const Pos *p) {
  if (eval_hand) return hce_evaluate(p);                     /* levels 1-3 */
  uintptr_t d = (uintptr_t)p - (uintptr_t)stk;                /* unsigned: a position elsewhere is far out */
  int ply = (int)(d / sizeof(Pos));
  if (!stk || d >= (MAXPLY + 2) * sizeof(Pos) || d % sizeof(Pos)) {   /* outside the search: from the board */
    int16_t a[2][NNX_HID];
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
  { int16_t t[2][NNX_HID]; nn_refresh(p, t);
    if (memcmp(t, nn_acc[ply], sizeof t)) { extern void xq_nn_check_failed(void); xq_nn_check_failed(); } }
#endif
  return nn_output(nn_acc[ply], p->side);
}
#else
int xq_evaluate(const Pos *p) { return hce_evaluate(p); }
#endif

/* ------------------------------------------------------------------ */
/*  Search: src/search.inc with Xiangqi's hooks                       */
/* ------------------------------------------------------------------ */

#define gen_moves  xq_gen_moves
#define gen_legal  xq_gen_legal
#define make_move  xq_make_move
#define in_check   xq_in_check
#define evaluate   xq_evaluate

static const uint8_t ORDV[8] = { 0, 9, 2, 2, 4, 6, 5, 1 };   /* victim and attacker for ordering */

static inline int32_t xq_tactical(const Pos *p, Move m) {
  if (!(XFLAGS(m) & XF_CAP)) return -1;
  return 1000000 + ORDV[XTYPE(p->b[XTO(m)])] * 100 - ORDV[XTYPE(p->b[XFROM(m)])];
}

static inline int xq_badcap(const Pos *p, Move m) {
  int att = XTYPE(p->b[XFROM(m)]), vic = XTYPE(p->b[XTO(m)]);
  if (att == XKING) return 0;
  return XVAL[att] - XVAL[vic] > 50 && xq_attacked(p, XTO(m), p->side ^ 1);
}

static int has_pieces(const Pos *p) {
  for (int r = 0; r < 10; r++)
    for (int f = 0; f < 9; f++) {
      int pc = p->b[XSQ(f, r)];
      if (pc && XCOLOR(pc) == p->side && (XTYPE(pc) == XCHARIOT || XTYPE(pc) == XHORSE || XTYPE(pc) == XCANNON))
        return 1;
    }
  return 0;
}

static inline void xq_null(const Pos *p, Pos *n) { *n = *p; n->side ^= 1; n->key ^= zside; n->half = 0; }

#define SEARCH_NAME            xq_search
#define G_TACTICAL(p, m)       xq_tactical(p, m)
#define G_ORDER_ADJ(m)         0
#define G_HIDX(p, m)           (((p)->b[XFROM(m)] << 8) | XTO(m))
#define G_HSIZE                (16 * 256)
#define G_QPRUNE(p, m, st, a)  ((st) + XVAL[XTYPE((p)->b[XTO(m)])] + 200 < (a))
#define G_DRAWN(p)             ((p)->half >= 120)
#define G_HAS_PIECES(p)        has_pieces(p)
#define G_NULL_MOVE(p, n)      xq_null(p, n)
#define G_QUIET(m)             (!(XFLAGS(m) & XF_CAP))
#define G_NOLEGAL(chk, ply)    (-MATE + (ply))
#define G_BADCAP(p, m)         xq_badcap(p, m)

#define PV_NAME xq_search_pv
#include "search.inc"
