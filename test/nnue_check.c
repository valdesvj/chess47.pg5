/* The evaluation network in C against tools/nnue/check.py (numpy): for each FEN of stdin, the evaluation from the
 * board; then a search on it, built with -DNN_CHECK: every accumulator updated in the search must equal a recount.
 *   gcc -O2 -DNN_CHECK -o build/nnue_check test/nnue_check.c src/engine.c src/xq_engine.c && python3 tools/nnue/check.py
 * (C) 2026 Victor Valdes - GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine.h"
#include "../src/xq_engine.h"
static void *xa(size_t n) { return malloc(n); }
void nn_check_failed(void) { puts("ACCUMULATOR MISMATCH"); exit(1); }
void xq_nn_check_failed(void) { puts("XQ ACCUMULATOR MISMATCH"); exit(1); }
int main(int argc, char **argv) {
  int xq = argc > 1 && !strcmp(argv[1], "xq");
  if (!eng_init(xa, 1 << 20) || !xq_init(xa)) return 1;
  char line[256];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\r\n|")] = 0;
    Limits L; memset(&L, 0, sizeof L); L.depth = 5;
    if (xq) {
      XPos p; if (!xq_from_fen(&p, line)) { puts("BADFEN"); continue; }
      int e = xq_evaluate(&p);
      eng_clear_tt(); xq_search(&p, &p.key, 1, &L);
      printf("%d\n", e);
    } else {
      Pos p; chess960 = 0; if (!pos_from_fen(&p, line)) { puts("BADFEN"); continue; }
      int e = evaluate(&p);
      eng_clear_tt(); search(&p, &p.key, 1, &L);
      printf("%d\n", e);
    }
  }
  return 0;
}
