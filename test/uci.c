/*
 * CHESS 47 - minimal UCI wrapper for testing the engine on a PC
 * (matches against Stockfish, GUIs like cutechess / Arena / BanksiaGUI).
 *
 *   go nodes N      node budget  (use it to emulate the DM42n speed)
 *   go movetime MS / wtime btime winc binc / depth D
 *   setoption name UCI_Chess960 value true
 *   setoption name Level value 0..9   (same levels as the calculator: noise/depth)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine.h"

static void *xa(size_t n) { return malloc(n); }
static uint32_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }

static Pos hist[MAXGAME + 2];
static uint64_t keys[MAXGAME + 2];
static int nh;
static int uci960;
static int level = -1;   /* -1 = full strength */
static int verbose = 1;

static void info(int d, int s, uint32_t n, Move b) {
  if (!verbose) return;
  char m[8];
  move_to_str(&hist[nh], b, m);
  if (IS_MATE(s)) printf("info depth %d score mate %d nodes %u pv %s\n", d, s > 0 ? (MATE - s + 1) / 2 : -(MATE + s) / 2, n, m);
  else printf("info depth %d score cp %d nodes %u pv %s\n", d, s, n, m);
  fflush(stdout);
}

static void uci_move(const Pos *p, Move m, char *s) {
  move_to_str(p, m, s);                 /* king->rook for castling */
  if ((MFLAGS(m) & F_CASTLE) && !uci960) {
    s[2] = (char)('a' + FILE_OF(MTO(m)));
    s[3] = (char)('1' + RANK_OF(MTO(m)));
  }
}

static void position(char *l) {
  char *t = strstr(l, "moves");
  if (t) t[-1] = 0;
  chess960 = uci960;
  if (strstr(l, "startpos")) { pos_start(&hist[0]); chess960 = uci960; }
  else {
    char *f = strstr(l, "fen ");
    if (!f || !pos_from_fen(&hist[0], f + 4)) return;
    chess960 = uci960;
  }
  nh = 0;
  keys[0] = hist[0].key;
  if (!t) return;
  t += 5;
  for (char *tok = strtok(t, " \n"); tok; tok = strtok(NULL, " \n")) {
    Move m = str_to_move(&hist[nh], tok);
    if (!m || nh >= MAXGAME) break;
    make_move(&hist[nh], &hist[nh + 1], m);
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
  Pos *p = &hist[nh];
  uint32_t mt = num(l, "movetime "), d = num(l, "depth "), nodes = num(l, "nodes ");
  uint32_t tl = num(l, p->side ? "btime " : "wtime "), inc = num(l, p->side ? "binc " : "winc ");
  if (mt) lim.time_ms = mt;
  else if (tl) { lim.time_ms = tl / 25 + inc * 3 / 4; if (lim.time_ms > tl / 3) lim.time_ms = tl / 3; }
  if (d) lim.depth = (int)d;
  if (nodes) lim.max_nodes = nodes;
  if (level >= 1 && level <= 9) { if (LV[level].depth < lim.depth) lim.depth = LV[level].depth; lim.noise = LV[level].noise; }
  Move best;
  if (level == 0) { Move ml[MAXMOVES]; int n = gen_legal(p, ml); best = n ? ml[rand() % n] : 0; }
  else best = search(p, keys, nh + 1, &lim).best;
  char s[8] = "0000";
  if (best) uci_move(p, best, s);
  printf("bestmove %s\n", s);
  fflush(stdout);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "-q")) verbose = 0;
  srand((unsigned)now_ms());
  if (!eng_init(xa, 64u << 20)) return 1;
  pos_start(&hist[0]);
  keys[0] = hist[0].key;
  char line[16384];
  while (fgets(line, sizeof line, stdin)) {
    if (!strncmp(line, "uci", 3) && (line[3] == '\n' || line[3] == '\r' || !line[3])) {
      puts("id name Chess47 1.0\nid author Victor Valdes");
      puts("option name UCI_Chess960 type check default false");
      puts("option name Level type spin default 10 min 0 max 10");
      puts("uciok");
    } else if (!strncmp(line, "isready", 7)) puts("readyok");
    else if (!strncmp(line, "ucinewgame", 10)) eng_clear_tt();
    else if (!strncmp(line, "setoption", 9)) {
      if (strstr(line, "UCI_Chess960")) uci960 = strstr(line, "true") != NULL;
      if (strstr(line, "name Level")) { char *v = strstr(line, "value "); level = v ? atoi(v + 6) : -1; if (level > 9) level = -1; }
    } else if (!strncmp(line, "position", 8)) position(line);
    else if (!strncmp(line, "go", 2)) go(line);
    else if (!strncmp(line, "quit", 4)) break;
    fflush(stdout);
  }
  return 0;
}
