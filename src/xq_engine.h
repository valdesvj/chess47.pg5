/*
 * CHESS47 for DM42n (DMCP5) - the Xiangqi (Chinese chess) engine
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 *
 * A 16 x 14 board (9 files x 10 ranks inside, padded for the 2-step horse and elephant), copy-make, the search of
 * src/search.inc (the same as chess's), sharing chess's hash table and move stacks (one game plays at a time).
 * Red moves first and sits at the bottom (ranks 0-4); Black at the top (ranks 5-9).
 * No platform dependencies: the same code builds for the calculator and the PC.
 */
#ifndef CHESS47_XQ_ENGINE_H
#define CHESS47_XQ_ENGINE_H

#include "engine_common.h"

/* moves: from, to (8 bits each), flags */
#define XMV(f,t,fl)  ((Move)(f) | ((Move)(t)<<8) | ((Move)(fl)<<24))
#define XFROM(m)     ((int)((m)&0xff))
#define XTO(m)       ((int)(((m)>>8)&0xff))
#define XFLAGS(m)    ((int)((m)>>24))
#define XF_CAP       1

/* pieces: type | colour << 3; Red 0, Black 1 */
enum { XEMPTY = 0, XKING = 1, XADVISOR, XELEPHANT, XHORSE, XCHARIOT, XCANNON, XPAWN };
#define XRED     0
#define XBLACK   1
#define XOFF     0x80                       /* off the board */
#define XTYPE(p)  ((p)&7)
#define XCOLOR(p) (((p)>>3)&1)
#define XSQ(f,r)  ((((r)+2)<<4)|((f)+3))     /* file 0-8 (a-i), rank 0-9 (Red's back rank 0) */
#define XFILE(s)  (((s)&15)-3)
#define XRANK(s)  (((s)>>4)-2)

typedef struct {
  uint8_t  b[256];
  uint8_t  side, half;                      /* half: plies since a capture */
  uint16_t fullmove;
  uint8_t  ksq[2];
  uint64_t key;
} XPos;

int  xq_init(void *(*alloc)(size_t));      /* after eng_init(): uses its hash table and move stacks */
void xq_pos_start(XPos *p);
int  xq_from_fen(XPos *p, const char *fen); /* rnbakabnr/... (Fairy-Stockfish, Pychess); h e accepted too */
void xq_to_fen(const XPos *p, char *out);
int  xq_attacked(const XPos *p, int sq, int by);
int  xq_in_check(const XPos *p);
int  xq_gen_moves(const XPos *p, Move *ml, int caps_only);    /* pseudo-legal */
int  xq_gen_legal(const XPos *p, Move *ml);
int  xq_make_move(const XPos *p, XPos *n, Move m);            /* 0 = illegal (own general attacked or facing) */
uint64_t xq_perft(const XPos *p, int depth);
/* coordinates: a-i and ranks from 0 (rank0 = 1: Pikafish, UCI xiangqi) or from 1 (rank0 = 0: Fairy-Stockfish) */
void xq_move_to_str(Move m, char *s, int rank0);
Move xq_str_to_move(const XPos *p, const char *s);             /* either numbering */
void xq_move_to_wxf(const XPos *p, Move m, char *s);           /* C2=5, H8+7, +R-1 (WXF, as Pychess) */
/* 0 playing, 1 checkmate, 2 stalemate (both lost by the side to move), 3 move limit (60 moves without a capture),
   4 no attacking pieces left, 5 threefold repetition */
int  xq_game_state(const XPos *p, const uint64_t *keys, int nkeys);
int  xq_evaluate(const XPos *p);
Result xq_search(const XPos *root, const uint64_t *keys, int nkeys, const Limits *lim);

#endif
