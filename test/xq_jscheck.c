/* xq_jscheck.c - the C Xiangqi engine's answers for web/xq_engine.js (test/test_web_xq.js compares them): for each
 * line of stdin "FEN|search depth|perft depth[|noise]": the perft count and a fixed-depth search (no time, the hash
 * table cleared, eng_init's 16 MB: the web engine's 2^20 entries): best move, WXF, score, depth, nodes, the key, the
 * FEN. GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine.h"
#include "../src/xq_engine.h"
static void *xa(size_t n) { return malloc(n); }
int main(void) {
  if (!eng_init(xa, 16u << 20) || !xq_init(xa)) return 1;
  char line[512];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\r\n")] = 0;
    char *a = strchr(line, '|'); if (!a) continue; *a++ = 0;
    char *b = strchr(a, '|'); if (!b) continue; *b++ = 0;
    char *nz = strchr(b, '|'); if (nz) *nz++ = 0;
    XPos p;
    if (!xq_from_fen(&p, line)) { printf("BADFEN\n"); continue; }
    unsigned long long pf = xq_perft(&p, atoi(b));
    eng_clear_tt();
    Limits lim; memset(&lim, 0, sizeof lim); lim.depth = atoi(a);
    if (nz) { lim.noise = atoi(nz); lim.seed = 12345; }
    uint64_t k = p.key;
    Result r = xq_search(&p, &k, 1, &lim);
    char m[12], w[12], fen[128];
    xq_move_to_str(r.best, m, 0);
    xq_move_to_wxf(&p, r.best, w);
    xq_to_fen(&p, fen);
    printf("%llu %s %s %d %d %u %08x%08x %s\n", pf, m, w, r.score, r.depth, r.nodes,
           (unsigned)(p.key >> 32), (unsigned)p.key, fen);
  }
  return 0;
}
