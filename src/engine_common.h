/*
 * CHESS47 for DM42n (DMCP5) - what the games' engines share: moves, the search's limits and result, the scores
 * (chess: engine.h, Xiangqi: xq_engine.h; the search itself: search.inc)
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 */
#ifndef CHESS47_ENGINE_COMMON_H
#define CHESS47_ENGINE_COMMON_H

#include <stdint.h>
#include <stddef.h>

#define MAXPLY    64
#define MAXMOVES  256
#define MAXGAME   400   /* plies kept in a game history */

typedef uint32_t Move;
#define INF   32000
#define MATE  31000
#define IS_MATE(s) ((s) > MATE-MAXPLY*2 || (s) < -MATE+MAXPLY*2)

/* Search */
typedef struct {
  int      depth;                 /* max depth (1..MAXPLY-4) */
  uint32_t time_ms;               /* soft time limit, 0 = none */
  int      noise;                 /* centipawns of root randomness (weak levels) */
  uint32_t seed;
  uint32_t max_nodes;             /* 0 = unlimited (used to emulate the DM42n speed on a PC) */
  uint32_t (*now)(void);          /* milliseconds clock */
  int      (*poll)(void);         /* return 1 to stop the search now */
  void     (*info)(int depth, int score, uint32_t nodes, Move best);
} Limits;

typedef struct {
  Move     best;
  int      score;
  int      depth;
  uint32_t nodes;
} Result;

/* the main lines' best moves, kept apart from the hash table (which overwrites them): the "thinking" display's expected
   line. Written at the search's main-line (PV) nodes only; read by search_pv / xq_search_pv */
typedef struct { uint64_t key; Move move; uint32_t pad; } PVE;
#define PVH_N 512

/* The tables the games share (one game plays at a time): the hash table, the move stacks, the killers, the
   repetition keys. src/xq_engine.c takes them from here instead of allocating its own. */
typedef struct {
  void     *tt;   uint32_t tt_n;
  Move     *mbuf; int32_t *sbuf;
  Move    (*killer)[2];
  uint64_t *rep;
  Move     *scratch;
  PVE      *pvh;
} EngShared;
const EngShared *eng_shared(void);
void eng_clear_tt(void);

#endif
