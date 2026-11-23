/*
 * King and pawn against king: which positions win (the "KPK bitbase"), by retrograde analysis, written to src/kpk.h.
 *   gcc -O2 -o build/kpk_gen tools/kpk_gen.c && build/kpk_gen > src/kpk.h
 * The pawn is White's on files a-d (the engine mirrors the others), ranks 2-7. Squares 0..63 = a1..h8.
 * Index: ((stm * 24 + (rank - 1) * 4 + file) * 64 + white king) * 64 + black king; a bit set = White wins.
 * A position is won when White promotes safely; drawn when Black takes the pawn or is stalemated; the rest follows
 * from the moves until nothing changes (what is still open then is a draw).
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>

enum { INVALID = 0, UNKNOWN = 1, DRAW = 2, WIN = 4 };
#define N (2 * 24 * 64 * 64)
static unsigned char db[N];

static int dist(int a, int b) {
  int df = abs((a & 7) - (b & 7)), dr = abs((a >> 3) - (b >> 3));
  return df > dr ? df : dr;
}
static int idx(int stm, int wk, int bk, int ps) { return ((stm * 24 + ((ps >> 3) - 1) * 4 + (ps & 7)) * 64 + wk) * 64 + bk; }
static int pawn_att(int ps, int s) { return (s >> 3) == (ps >> 3) + 1 && abs((s & 7) - (ps & 7)) == 1; }

static unsigned char init(int stm, int wk, int bk, int ps) {
  if (dist(wk, bk) <= 1 || wk == ps || bk == ps || (stm == 0 && pawn_att(ps, bk))) return INVALID;
  int q = ps + 8;
  if (stm == 0 && (ps >> 3) == 6 && wk != q && (dist(bk, q) > 1 || dist(wk, q) == 1)) return WIN;
  if (stm == 1) {
    int free = 0, take = 0;
    for (int s = 0; s < 64; s++) {
      if (dist(bk, s) != 1) continue;
      if (dist(wk, s) <= 1) continue;
      if (s == ps) { take = 1; continue; }
      if (pawn_att(ps, s)) continue;
      free = 1;
    }
    if (take || !free) return DRAW;
  }
  return UNKNOWN;
}

static unsigned char classify(int stm, int wk, int bk, int ps) {
  unsigned char r = INVALID, good = stm == 0 ? WIN : DRAW, bad = stm == 0 ? DRAW : WIN;
  int k = stm == 0 ? wk : bk;
  for (int s = 0; s < 64; s++) {
    if (dist(k, s) != 1) continue;
    r |= stm == 0 ? db[idx(1, s, bk, ps)] : db[idx(0, wk, s, ps)];
  }
  if (stm == 0) {
    if ((ps >> 3) < 6) r |= db[idx(1, wk, bk, ps + 8)];
    if ((ps >> 3) == 1 && ps + 8 != wk && ps + 8 != bk) r |= db[idx(1, wk, bk, ps + 16)];
  }
  return r & good ? good : r & UNKNOWN ? UNKNOWN : bad;
}

int main(void) {
  for (int stm = 0; stm < 2; stm++)
    for (int ps = 8; ps < 56; ps++) {
      if ((ps & 7) > 3) continue;
      for (int wk = 0; wk < 64; wk++)
        for (int bk = 0; bk < 64; bk++) db[idx(stm, wk, bk, ps)] = init(stm, wk, bk, ps);
    }
  for (int changed = 1; changed;) {
    changed = 0;
    for (int stm = 0; stm < 2; stm++)
      for (int ps = 8; ps < 56; ps++) {
        if ((ps & 7) > 3) continue;
        for (int wk = 0; wk < 64; wk++)
          for (int bk = 0; bk < 64; bk++) {
            int i = idx(stm, wk, bk, ps);
            if (db[i] != UNKNOWN) continue;
            unsigned char r = classify(stm, wk, bk, ps);
            if (r != UNKNOWN) { db[i] = r; changed = 1; }
          }
      }
  }
  int wins = 0;
  static unsigned char bits[N / 8];
  for (int i = 0; i < N; i++) if (db[i] == WIN) { bits[i >> 3] |= (unsigned char)(1 << (i & 7)); wins++; }
  printf("/*\n * CHESS47 for DM42n (DMCP5) - king and pawn against king: the won positions (tools/kpk_gen.c, generated)\n"
         " * %d positions won with White (the pawn's side) as given\n * (C) 2026 Victor Valdes - GPL-3.0-or-later\n */\n", wins);
  printf("static const uint8_t KPK[%d] = {\n", N / 8);
  for (int i = 0; i < N / 8; i++) printf("%s%d%s", i % 24 ? "" : "  ", bits[i], i == N / 8 - 1 ? "\n" : i % 24 == 23 ? ",\n" : ",");
  printf("};\n");
  return 0;
}
