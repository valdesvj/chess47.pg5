/*
 * CHESS47 for DM42n (DMCP5) - portable chess engine
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 *
 * 0x88 board, copy-make, legal move generation with Chess960 castling,
 * PVS alpha-beta with transposition table, null move, LMR and quiescence.
 * No platform dependencies: the same code builds for the calculator and the PC.
 */
#ifndef CHESS47_ENGINE_H
#define CHESS47_ENGINE_H

#include <stdint.h>
#include <stddef.h>

#define MAXPLY    64
#define MAXMOVES  256
#define MAXGAME   400   /* plies kept in a game history */

typedef uint32_t Move;
#define MV(f,t,pr,fl) ((Move)(f) | ((Move)(t)<<8) | ((Move)(pr)<<16) | ((Move)(fl)<<24))
#define MFROM(m)  ((int)((m)&0x7f))
#define MTO(m)    ((int)(((m)>>8)&0x7f))
#define MPROMO(m) ((int)(((m)>>16)&0xf))
#define MFLAGS(m) ((int)((m)>>24))

#define F_CAP    1
#define F_EP     2
#define F_CASTLE 4
#define F_DPUSH  8
#define F_QSIDE  16

enum { EMPTY = 0, PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };
#define WHITE 0
#define BLACK 1
#define BLK   8
#define PTYPE(p)   ((p)&7)
#define PCOLOR(p)  ((p)>>3)
#define SQ(f,r)    (((r)<<4)|(f))
#define FILE_OF(s) ((s)&7)
#define RANK_OF(s) ((s)>>4)
#define NOSQ       0x7f
#define ONBOARD(s) (!((s)&0x88))

#define INF   32000
#define MATE  31000
#define IS_MATE(s) ((s) > MATE-MAXPLY*2 || (s) < -MATE+MAXPLY*2)

typedef struct {
  uint8_t  b[128];
  uint8_t  side, castle, ep, half;
  uint16_t fullmove;
  uint8_t  ksq[2];
  uint64_t key;
} Pos;

/* Castling geometry of the current game (shared by both colours, as in 960) */
extern uint8_t c_rfile[2];   /* [0] king-side rook file, [1] queen-side rook file */
extern int     chess960;     /* 1 = write Shredder-FEN castling, X-FEN aware parser */

/* Memory: the engine allocates its tables once with this allocator */
int  eng_init(void *(*alloc)(size_t), size_t tt_bytes);
size_t eng_tt_bytes(void);
void eng_clear_tt(void);

/* Positions */
int  pos_from_fen(Pos *p, const char *fen);
void pos_to_fen(const Pos *p, char *out);
void pos_start(Pos *p);
void pos_960(Pos *p, int n);          /* Scharnagl number 0..959 (518 = classic) */
int  attacked(const Pos *p, int sq, int by);
int  in_check(const Pos *p);
int  insufficient(const Pos *p);

/* Moves */
int  gen_moves(const Pos *p, Move *ml, int caps_only);   /* pseudo-legal */
int  gen_legal(const Pos *p, Move *ml);
int  make_move(const Pos *p, Pos *n, Move m);            /* 0 = illegal */
int  castle_rook_sq(const Pos *p, Move m);
void move_to_str(const Pos *p, Move m, char *s);        /* e2e4, e7e8q, castle = king->rook */
void move_to_san(const Pos *p, Move m, char *s);
Move str_to_move(const Pos *p, const char *s);
uint64_t perft(const Pos *p, int depth);

/* Game result for side to move: 0 playing, 1 checkmate, 2 stalemate,
   3 fifty moves, 4 insufficient material, 5 threefold repetition */
int  game_state(const Pos *p, const uint64_t *keys, int nkeys);

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

Result search(const Pos *root, const uint64_t *keys, int nkeys, const Limits *lim);
int  evaluate(const Pos *p);

#endif
