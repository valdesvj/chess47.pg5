/* jscheck.c - the C engine's answers for web/engine.js (test/test_web.js compares them): for each line of
 * stdin "FEN|search depth|perft depth[|noise]": the perft count and a fixed-depth search (no time, no noise, the hash
 * table cleared, 16 MB: the web engine's 2^20 entries): best move, score, depth, nodes. MIT License. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine.h"
static void *xa(size_t n) { return malloc(n); }
int main(void) {
  if (!eng_init(xa, 16u << 20)) return 1;
  char line[512];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\r\n")] = 0;
    char *a = strchr(line, '|'); if (!a) continue; *a++ = 0;
    char *b = strchr(a, '|'); if (!b) continue; *b++ = 0;
    char *nz = strchr(b, '|'); if (nz) *nz++ = 0;          /* optional: the root noise (seed 12345) */
    Pos p; chess960 = 0;
    if (!pos_from_fen(&p, line)) { printf("BADFEN\n"); continue; }
    unsigned long long pf = perft(&p, atoi(b));
    eng_clear_tt();
    Limits lim; memset(&lim, 0, sizeof lim); lim.depth = atoi(a);
    if (nz) { lim.noise = atoi(nz); lim.seed = 12345; }
    uint64_t k = p.key;
    Result r = search(&p, &k, 1, &lim);
    char m[8]; move_to_str(&p, r.best, m);
    char san[16]; move_to_san(&p, r.best, san);
    char fen[100]; pos_to_fen(&p, fen);
    printf("%llu %s %s %d %d %u %08x%08x %s\n", pf, m, san, r.score, r.depth, r.nodes,
           (unsigned)(p.key >> 32), (unsigned)p.key, fen);
  }
  return 0;
}
