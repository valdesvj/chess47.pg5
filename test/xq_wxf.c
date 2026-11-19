/* Host test: every legal move of each FEN (stdin) with its coordinates (ranks 1-10, as Fairy-Stockfish) and the
 * engine's WXF; test/xq_check.py compares it with Fairy-Stockfish's. (C) 2026 Victor Valdes - GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine.h"
#include "../src/xq_engine.h"

static void *xa(size_t n) { return malloc(n); }

int main(void) {
  if (!eng_init(xa, 1 << 20) || !xq_init(xa)) return 1;
  char line[256];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = 0;
    XPos p;
    if (!xq_from_fen(&p, line)) continue;
    Move ml[MAXMOVES];
    int n = xq_gen_legal(&p, ml);
    for (int i = 0; i < n; i++) {
      char c[16], w[16];
      xq_move_to_str(ml[i], c, 0);
      xq_move_to_wxf(&p, ml[i], w);
      printf("%s %s %s\n", line, c, w);
    }
  }
  return 0;
}
