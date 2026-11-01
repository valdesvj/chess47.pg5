/* Host test: perft node counts against published values, plus a search smoke test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine.h"

static void *xalloc(size_t n) { return malloc(n); }
static uint32_t now_ms(void) { return (uint32_t)(clock() * 1000.0 / CLOCKS_PER_SEC); }

typedef struct { const char *fen; int depth; uint64_t nodes; } T;

int main(void) {
  static const T tests[] = {
    { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609ULL },
    { "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603ULL },
    { "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624ULL },
    { "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333ULL },
    { "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487ULL },
    { "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594ULL },
    /* Chess960 (Shredder-FEN) */
    { "bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9", 4, 326672ULL },
    { "2nnrbkr/p1qppppp/8/1ppb4/6PP/3PP3/PPP2P2/BQNNRBKR w HEhe - 1 9", 4, 667366ULL },
  };
  if (!eng_init(xalloc, 1 << 22)) { puts("init failed"); return 1; }
  int fails = 0;
  for (unsigned i = 0; i < sizeof tests / sizeof *tests; i++) {
    Pos p;
    chess960 = 0;
    if (!pos_from_fen(&p, tests[i].fen)) { printf("FEN FAIL %s\n", tests[i].fen); fails++; continue; }
    uint64_t n = perft(&p, tests[i].depth);
    int ok = n == tests[i].nodes;
    fails += !ok;
    printf("%s d%d %llu %s\n", ok ? "OK  " : "FAIL", tests[i].depth, (unsigned long long)n, tests[i].fen);
  }
  /* 960 numbering: 518 must be the classic setup */
  {
    Pos p; char fen[100];
    pos_960(&p, 518); pos_to_fen(&p, fen);
    printf("960 #518 -> %s\n", fen);
    pos_960(&p, 0); pos_to_fen(&p, fen);
    printf("960 #0   -> %s\n", fen);
    uint64_t n = perft(&p, 3);
    printf("960 #0 perft3 %llu\n", (unsigned long long)n);
  }
  /* SAN / string round trip */
  {
    Pos p; Move ml[MAXMOVES]; char s[16], u[8];
    pos_from_fen(&p, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    int n = gen_legal(&p, ml);
    for (int i = 0; i < n; i++) {
      move_to_san(&p, ml[i], s); move_to_str(&p, ml[i], u);
      if (str_to_move(&p, u) != ml[i]) { printf("ROUNDTRIP FAIL %s\n", u); fails++; }
      printf("%s ", s);
    }
    printf("\n");
  }
  /* Search smoke tests */
  {
    const char *fens[] = {
      "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",                 /* Rd8# */
      "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", /* Qxf7# */
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R2QKB1R w KQ - 0 8",
    };
    for (int i = 0; i < 4; i++) {
      Pos p; char s[16];
      chess960 = 0;
      pos_from_fen(&p, fens[i]);
      eng_clear_tt();
      Limits lim = { 30, 3000, 0, 1, 0, now_ms, NULL, NULL };
      uint32_t a = now_ms();
      Result r = search(&p, &p.key, 1, &lim);
      move_to_san(&p, r.best, s);
      printf("search: %-8s score %6d depth %2d nodes %8u  %u ms  (%s)\n", s, r.score, r.depth, r.nodes, now_ms() - a, fens[i]);
    }
  }
  printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
  return fails != 0;
}
