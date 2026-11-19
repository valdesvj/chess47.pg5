/* Host test: a fixed-depth search on a few positions; prints the best move, score, depth and nodes.
 * The shared search (src/search.inc) must give chess the same numbers as before it existed:
 *   make bench > build/bench.txt      (compare with test/bench_chess.txt)
 * (C) 2026 Victor Valdes - GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine.h"

static void *xa(size_t n) { return malloc(n); }

int main(void) {
  static const char *fens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
    "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
  };
  if (!eng_init(xa, 1 << 22)) { puts("init failed"); return 1; }
  for (unsigned i = 0; i < sizeof fens / sizeof *fens; i++) {
    Pos p; char s[16];
    chess960 = 0;
    pos_from_fen(&p, fens[i]);
    eng_clear_tt();
    Limits L; memset(&L, 0, sizeof L);
    L.depth = 7;
    Result r = search(&p, &p.key, 1, &L);
    move_to_str(&p, r.best, s);
    printf("%-6s %6d d%d %9u  %s\n", s, r.score, r.depth, r.nodes, fens[i]);
  }
  return 0;
}
