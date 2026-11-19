/* Host test of the Xiangqi engine: perft of FENs (stdin, "depth fen" a line; prints "nodes"), or with no input the
 * start position's published counts. test/xq_check.py compares it with Fairy-Stockfish (pyffish) on many positions.
 * (C) 2026 Victor Valdes - GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine.h"
#include "../src/xq_engine.h"

static void *xa(size_t n) { return malloc(n); }

int main(int argc, char **argv) {
  if (!eng_init(xa, 1 << 22) || !xq_init(xa)) { puts("init failed"); return 1; }
  if (argc > 1 && !strcmp(argv[1], "-")) {
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
      int d = atoi(line);
      char *f = strchr(line, ' ');
      if (!f) continue;
      XPos p;
      if (!xq_from_fen(&p, f + 1)) { puts("BADFEN"); continue; }
      printf("%llu\n", (unsigned long long)xq_perft(&p, d));
      fflush(stdout);
    }
    return 0;
  }
  static const uint64_t want[] = { 0, 44, 1920, 79666, 3290240, 133312995 };
  XPos p; char fen[120];
  xq_pos_start(&p);
  xq_to_fen(&p, fen);
  printf("%s\n", fen);
  int fails = 0;
  for (int d = 1; d <= 5; d++) {
    uint64_t n = xq_perft(&p, d);
    printf("%s d%d %llu\n", n == want[d] ? "OK  " : "FAIL", d, (unsigned long long)n);
    fails += n != want[d];
  }
  puts(fails ? "FAILED" : "ALL PASS");
  return fails != 0;
}
