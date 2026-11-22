/*
 * CHESS 47 - the Xiangqi engine over UCI, for testing on a PC (matches against Fairy-Stockfish: test/xq_match.py;
 * GUIs that speak UCI Xiangqi). Moves and FEN as Fairy-Stockfish writes them: files a-i, ranks 1-10 from Red's side
 * (h3e3 = C2=5), pieces rnbakabnr / RNBAKABNR.
 *
 *   go nodes N      node budget  (use it to emulate the DM42n speed)
 *   go movetime MS / wtime btime winc binc / depth D
 *   setoption name Level value 0..9   (the calculator's levels: noise and depth)
 *
 *   gcc -O2 -o build/xq-uci test/xq_uci.c src/engine.c src/xq_engine.c
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine.h"
#include "../src/xq_engine.h"

static void *xa(size_t n) { return malloc(n); }
static uint32_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }

static XPos hist[MAXGAME + 2];
static uint64_t keys[MAXGAME + 2];
static int nh;
static int level = -1;   /* -1 = full strength */
static int verbose = 1;

static void info(int d, int s, uint32_t n, Move b) {
  if (!verbose) return;
  char m[12];
  xq_move_to_str(b, m, 0);
  if (IS_MATE(s)) printf("info depth %d score mate %d nodes %u pv %s\n", d, s > 0 ? (MATE - s + 1) / 2 : -(MATE + s) / 2, n, m);
  else printf("info depth %d score cp %d nodes %u pv %s\n", d, s, n, m);
  fflush(stdout);
}

/* a move in Fairy-Stockfish's numbering (ranks 1-10) only: the other numbering could read as another legal move */
static Move parse(const XPos *p, const char *tok) {
  Move ml[MAXMOVES];
  int n = xq_gen_legal(p, ml);
  char s[12];
  for (int i = 0; i < n; i++) { xq_move_to_str(ml[i], s, 0); if (!strcmp(s, tok)) return ml[i]; }
  return 0;
}

static void position(char *l) {
  char *t = strstr(l, "moves");
  if (t) t[-1] = 0;
  if (strstr(l, "startpos")) xq_pos_start(&hist[0]);
  else {
    char *f = strstr(l, "fen ");
    if (!f || !xq_from_fen(&hist[0], f + 4)) return;
  }
  nh = 0;
  keys[0] = hist[0].key;
  if (!t) return;
  t += 5;
  for (char *tok = strtok(t, " \r\n"); tok; tok = strtok(NULL, " \r\n")) {
    Move m = parse(&hist[nh], tok);
    if (!m || nh >= MAXGAME) break;
    xq_make_move(&hist[nh], &hist[nh + 1], m);
    nh++;
    keys[nh] = hist[nh].key;
  }
}

static uint32_t num(const char *l, const char *k) { const char *p = strstr(l, k); return p ? (uint32_t)strtoul(p + strlen(k), NULL, 10) : 0; }

static void go(const char *l) {
  static const struct { int depth; int noise; } LV[10] = { {1,0},{1,150},{2,80},{3,40},{5,6},{40,2},{40,0},{40,0},{40,0},{40,0} };
  Limits lim;
  memset(&lim, 0, sizeof lim);
  lim.depth = 60;
  lim.now = now_ms;
  lim.info = info;
  lim.seed = (uint32_t)rand();
  XPos *p = &hist[nh];
  uint32_t mt = num(l, "movetime "), d = num(l, "depth "), nodes = num(l, "nodes ");
  uint32_t tl = num(l, p->side ? "btime " : "wtime "), inc = num(l, p->side ? "binc " : "winc ");
  if (mt) lim.time_ms = mt;
  else if (tl) { lim.time_ms = tl / 25 + inc * 3 / 4; if (lim.time_ms > tl / 3) lim.time_ms = tl / 3; }
  if (d) lim.depth = (int)d;
  if (nodes) lim.max_nodes = nodes;
  if (level >= 1 && level <= 9) { if (LV[level].depth < lim.depth) lim.depth = LV[level].depth; lim.noise = LV[level].noise; }
  Move best;
  if (level == 0) { Move ml[MAXMOVES]; int n = xq_gen_legal(p, ml); best = n ? ml[rand() % n] : 0; }
  else best = xq_search(p, keys, nh + 1, &lim).best;
  char s[12] = "0000";
  if (best) xq_move_to_str(best, s, 0);
  printf("bestmove %s\n", s);
  fflush(stdout);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "-q")) verbose = 0;
  srand((unsigned)now_ms());
  /* the hash table: 64 MB, or C47_HASH_KB (the calculator has up to 1024 KB: tests at its size) */
  size_t hb = getenv("C47_HASH_KB") ? (size_t)atoi(getenv("C47_HASH_KB")) << 10 : 64u << 20;
  if (!eng_init(xa, hb) || !xq_init(xa)) return 1;
  xq_pos_start(&hist[0]);
  keys[0] = hist[0].key;
  char line[16384];
  while (fgets(line, sizeof line, stdin)) {
    if (!strncmp(line, "uci", 3) && (line[3] == '\n' || line[3] == '\r' || !line[3])) {
      puts("id name Xiangqi47 1.1\nid author Victor Valdes");
      puts("option name UCI_Variant type combo default xiangqi var xiangqi");
      puts("option name Level type spin default 10 min 0 max 10");
      puts("uciok");
    } else if (!strncmp(line, "isready", 7)) puts("readyok");
    else if (!strncmp(line, "ucinewgame", 10)) eng_clear_tt();
    else if (!strncmp(line, "setoption", 9)) {
      if (strstr(line, "name Level")) { char *v = strstr(line, "value "); level = v ? atoi(v + 6) : -1; if (level > 9) level = -1; }
    } else if (!strncmp(line, "position", 8)) position(line);
    else if (!strncmp(line, "go", 2)) go(line);
    else if (!strncmp(line, "quit", 4)) break;
    fflush(stdout);
  }
  return 0;
}
