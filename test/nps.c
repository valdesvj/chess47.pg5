/* Host speed test: positions a second of the chess and Xiangqi searches (a fixed depth on a few positions each).
 *   gcc -O2 -o build/nps test/nps.c src/engine.c src/xq_engine.c && build/nps
 * (C) 2026 Victor Valdes - GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine.h"
#include "../src/xq_engine.h"

static void *xa(size_t n) { return malloc(n); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
  static const char *cf[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP1B1PPP/R2QKB1R w KQ - 0 8",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "6k1/pp3ppp/4p3/8/3P4/2r3P1/P4P1P/3R2K1 w - - 0 25",
  };
  static const char *xf[] = {
    "rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1",
    "r1bakabr1/9/1cn3nc1/p1p1p1p1p/9/9/P1P1P1P1P/1CN3NC1/9/R1BAKAB1R w - - 0 1",
    "2bakab2/9/2n1c1n2/p1p1p1p1p/9/2P6/P3P1P1P/2N1C1N2/9/2BAKAB2 w - - 0 1",
    "3k5/4a4/4b4/9/9/9/9/4B4/4A4/3K1R3 w - - 0 1",
  };
  int d = argc > 1 ? atoi(argv[1]) : 9, dx = argc > 2 ? atoi(argv[2]) : 8;
  if (!eng_init(xa, 1 << 20) || !xq_init(xa)) return 1;
  double t = now(); uint64_t n = 0;
  for (unsigned i = 0; i < sizeof cf / sizeof *cf; i++) {
    Pos p; chess960 = 0; pos_from_fen(&p, cf[i]); eng_clear_tt();
    Limits L; memset(&L, 0, sizeof L); L.depth = d;
    n += search(&p, &p.key, 1, &L).nodes;
  }
  t = now() - t;
  printf("chess   d%d %10llu nodes %6.2f s %8.0f nps\n", d, (unsigned long long)n, t, n / t);
  t = now(); n = 0;
  for (unsigned i = 0; i < sizeof xf / sizeof *xf; i++) {
    XPos p; xq_from_fen(&p, xf[i]); eng_clear_tt();
    Limits L; memset(&L, 0, sizeof L); L.depth = dx;
    n += xq_search(&p, &p.key, 1, &L).nodes;
  }
  t = now() - t;
  printf("xiangqi d%d %10llu nodes %6.2f s %8.0f nps\n", dx, (unsigned long long)n, t, n / t);
  return 0;
}
