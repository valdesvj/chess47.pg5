/*
 * NNUE training data, step 1: Chess 47's own games (chess or Xiangqi) from random openings, at a small node budget.
 * Writes the quiet positions (not in check, the engine's move not a capture) with the game's result:
 *   FEN|result      (result from White's / Red's side: 1, 0.5, 0)
 * Step 2 (tools/nnue/label.py) adds Stockfish's / Pikafish's score.
 *
 *   gcc -O2 -o build/nnue-gen tools/nnue/gen.c src/engine.c src/xq_engine.c
 *   build/nnue-gen chess|xq GAMES SEED NODES > out.txt
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/engine.h"
#include "../../src/xq_engine.h"

static void *xa(size_t n) { return malloc(n); }
static uint64_t rs;
static uint32_t rnd(uint32_t n) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs % n); }

#define MAXP 400
static char fens[MAXP][100];

static void flush_game(int n, const char *res) {
  for (int i = 0; i < n; i++) printf("%s|%s\n", fens[i], res);
}

static void chess_game(uint32_t nodes) {
  static Pos h[MAXP + 20];
  static uint64_t keys[MAXP + 20];
  Move ml[MAXMOVES];
  int ply = 0, nf = 0;
  chess960 = 0;
  pos_start(&h[0]);
  keys[0] = h[0].key;
  int rp = 8 + (int)rnd(5);                          /* random opening plies */
  const char *res = "0.5";
  eng_clear_tt();
  for (;;) {
    Pos *p = &h[ply];
    int st = game_state(p, keys, ply + 1);
    if (st) { res = st == 1 ? (p->side == WHITE ? "0" : "1") : "0.5"; break; }
    if (ply >= MAXP) break;
    Move m;
    if (ply < rp) {
      int n = gen_legal(p, ml);
      m = ml[rnd(n)];
    } else {
      Limits L; memset(&L, 0, sizeof L);
      L.depth = 40; L.max_nodes = nodes;
      Result r = search(p, keys, ply + 1, &L);
      m = r.best;
      if (IS_MATE(r.score) && r.score > 0 && r.depth >= 4) { res = p->side == WHITE ? "1" : "0"; break; }
      if (IS_MATE(r.score) && r.score < 0 && r.depth >= 4) { res = p->side == WHITE ? "0" : "1"; break; }
      if (!in_check(p) && !(MFLAGS(m) & F_CAP) && !MPROMO(m) && ply >= rp + 2 && nf < MAXP)
        pos_to_fen(p, fens[nf++]);
    }
    make_move(p, &h[ply + 1], m);
    ply++;
    keys[ply] = h[ply].key;
  }
  flush_game(nf, res);
}

static void xq_game(uint32_t nodes) {
  static XPos h[MAXP + 20];
  static uint64_t keys[MAXP + 20];
  Move ml[MAXMOVES];
  int ply = 0, nf = 0;
  xq_pos_start(&h[0]);
  keys[0] = h[0].key;
  int rp = 6 + (int)rnd(5);
  const char *res = "0.5";
  eng_clear_tt();
  for (;;) {
    XPos *p = &h[ply];
    int st = xq_game_state(p, keys, ply + 1);
    if (st) { res = st <= 2 ? (p->side == XRED ? "0" : "1") : "0.5"; break; }
    if (ply >= 300) break;
    Move m;
    if (ply < rp) {
      int n = xq_gen_legal(p, ml);
      m = ml[rnd(n)];
    } else {
      Limits L; memset(&L, 0, sizeof L);
      L.depth = 40; L.max_nodes = nodes;
      Result r = xq_search(p, keys, ply + 1, &L);
      m = r.best;
      if (IS_MATE(r.score) && r.depth >= 4) { res = (r.score > 0) == (p->side == XRED) ? "1" : "0"; break; }
      if (!xq_in_check(p) && !(XFLAGS(m) & XF_CAP) && ply >= rp + 2 && nf < MAXP) xq_to_fen(p, fens[nf++]);
    }
    xq_make_move(p, &h[ply + 1], m);
    ply++;
    keys[ply] = h[ply].key;
  }
  flush_game(nf, res);
}

int main(int argc, char **argv) {
  if (argc < 5) { fprintf(stderr, "nnue-gen chess|xq GAMES SEED NODES\n"); return 1; }
  int xq = !strcmp(argv[1], "xq"), games = atoi(argv[2]);
  rs = 0x9E3779B97F4A7C15ULL ^ ((uint64_t)atoi(argv[3]) * 0x2545F4914F6CDD1DULL);
  if (!rs) rs = 1;
  uint32_t nodes = (uint32_t)atoi(argv[4]);
  if (!eng_init(xa, 4u << 20) || !xq_init(xa)) return 1;
  for (int g = 0; g < games; g++) { if (xq) xq_game(nodes); else chess_game(nodes); }
  return 0;
}
