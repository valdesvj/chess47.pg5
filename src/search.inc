/*
 * CHESS47 for DM42n (DMCP5) - the search shared by the games (chess: src/engine.c, Xiangqi: src/xq_engine.c)
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 *
 * Included at the end of a game's engine file, which first defines its Pos and Move, the tables below and these
 * hooks (each game compiles its own copy: no function pointers in the inner loops):
 *   SEARCH_NAME              the public search function (search, xq_search)
 *   G_TACTICAL(p, m)         the ordering score of a capture (or queen promotion), or -1 for a quiet move
 *   G_ORDER_ADJ(m)           added to every ordering score (chess: under-promotions last)
 *   G_HIDX(p, m), G_HSIZE    the history table's index of a move and its size
 *   G_QPRUNE(p, m, st, a)    1: skip this capture in quiescence (delta pruning)
 *   G_DRAWN(p)               a draw by rule in the search (chess: 50 moves)
 *   G_HAS_PIECES(p)          the side to move has pieces for a null move (no zugzwang worry)
 *   G_NULL_MOVE(p, n)        n = p with the move passed (side, key, en passant)
 *   G_QUIET(m)               not a capture and not a promotion (LMR, killers, history)
 *   G_NOLEGAL(chk, ply)      the score with no legal move (chess: mated or stalemate 0; Xiangqi: lost either way)
 *   G_BADCAP(p, m)           1: a capture likely to lose material (the capturer worth more, the point defended)
 * and the tables: tt, tt_n, stk, mbuf, sbuf, mofs, killer, hh, rep, rep_n, scratch, nodes; MBUF; evaluate,
 * gen_moves, gen_legal, make_move, in_check.
 */

#ifndef SP_RFP
#define SP_RFP 90        /* reverse futility: the static score this far above beta per ply left (depth <= 3) */
#endif
#ifndef SP_FUT
#define SP_FUT 150       /* futility: quiet moves skipped when the static score is this far below alpha a ply */
#endif
#ifndef SP_LMP
#define SP_LMP 4         /* late move pruning: quiet moves after 4 + 2 depth^2 tries skipped (depth <= 3) */
#endif
#ifndef SP_MDP
#define SP_MDP 1         /* mate distance pruning */
#endif
#ifndef SP_ASP
#define SP_ASP 25        /* aspiration window at the root (centipawns, 0 = off) from depth 5 */
#endif
#ifndef SP_HMAL
#define SP_HMAL 1        /* history: the quiet moves tried before a cut lose what the cut move gains (gravity) */
#endif
#ifndef SP_QTT
#define SP_QTT 1         /* quiescence: the hash table's bounds used */
#endif
#ifndef SP_LMRT
#define SP_LMRT 1        /* late move reductions from a log table instead of 1 / 2 */
#endif
#ifndef SP_BADCAP
#define SP_BADCAP 2      /* captures that lose material (G_BADCAP): 1 skipped in quiescence, 2 also ordered last */
#endif
#ifndef SP_TT2
#define SP_TT2 0         /* the hash table in buckets of two: the shallower or older entry gives way */
#endif

static int      stop;
static uint32_t t0;
static const Limits *L;
static Move     root_best;
static int      root_score;
static uint8_t  pv_age;                /* the search's number: the main-line table's older entries give way */
#if SP_LMRT
#include "lmr.h"                       /* LMR_T[depth 0..31][move 0..63] */
#endif

static void check_stop(void) {
  if (L->max_nodes && nodes >= L->max_nodes) { stop = 1; return; }
  if (L->poll && L->poll()) { stop = 1; return; }
  if (L->time_ms && L->now && (uint32_t)(L->now() - t0) >= L->time_ms) stop = 1;
}

static int is_rep(const Pos *p) {
  for (int i = rep_n - 3, k = 2; i >= 0 && k <= p->half; i -= 2, k += 2)
    if (rep[i] == p->key) return 1;
  return 0;
}

/* the hash table's entry of a position: its own, else (storing) the one to replace. The flag's bits 0-1 are the
   bound, 2-7 the search's number (SP_TT2: the age) */
#define TT_BOUND(e) ((e)->flag & 3)
static TTE *tt_entry(uint64_t key) {
#if SP_TT2
  TTE *e = &tt[key & (tt_n - 2)];
  if (e[0].key == key) return e;
  if (e[1].key == key) return e + 1;
  int a0 = (uint8_t)(pv_age - (e[0].flag >> 2)) & 63, a1 = (uint8_t)(pv_age - (e[1].flag >> 2)) & 63;
  return e[0].depth - 8 * a0 <= e[1].depth - 8 * a1 ? e : e + 1;
#else
  return &tt[key & (tt_n - 1)];
#endif
}

static int score_to_tt(int s, int ply) { return s > MATE - 2 * MAXPLY ? s + ply : s < -MATE + 2 * MAXPLY ? s - ply : s; }
static int score_from_tt(int s, int ply) { return s > MATE - 2 * MAXPLY ? s - ply : s < -MATE + 2 * MAXPLY ? s + ply : s; }

static void order(const Pos *p, Move *ml, int32_t *sc, int n, Move ttm, int ply) {
  for (int i = 0; i < n; i++) {
    Move m = ml[i];
    int32_t v;
    if (m == ttm) v = 1 << 30;
    else if ((v = G_TACTICAL(p, m)) >= 0) { if (SP_BADCAP >= 2 && G_BADCAP(p, m)) v -= 3000000; }
    else if (m == killer[ply][0]) v = 900000;
    else if (m == killer[ply][1]) v = 800000;
    else v = hh[G_HIDX(p, m)];
    sc[i] = v + G_ORDER_ADJ(m);
  }
}

static Move pick(Move *ml, int32_t *sc, int n, int i) {
  int bi = i;
  for (int j = i + 1; j < n; j++) if (sc[j] > sc[bi]) bi = j;
  Move m = ml[bi]; int32_t s = sc[bi];
  ml[bi] = ml[i]; sc[bi] = sc[i];
  ml[i] = m; sc[i] = s;
  return m;
}

static int qsearch(int ply, int alpha, int beta) {
  Pos *p = &stk[ply];
  if ((++nodes & 1023) == 0) check_stop();
  if (stop) return 0;
#if SP_QTT
  {
    const TTE *e = tt_entry(p->key);
    if (e->key == p->key && e->depth >= 0 && beta - alpha == 1) {
      int s = score_from_tt(e->score, ply), f = TT_BOUND(e);
      if (f == TT_EXACT || (f == TT_LOWER && s >= beta) || (f == TT_UPPER && s <= alpha)) return s;
    }
  }
#endif
  int stand = evaluate(p);
  if (ply >= MAXPLY - 1) return stand;
  if (stand >= beta) return stand;
  if (stand > alpha) alpha = stand;
  if (mofs[ply] + MAXMOVES > MBUF) return stand;
  Move *ml = mbuf + mofs[ply];
  int32_t *sc = sbuf + mofs[ply];
  int n = gen_moves(p, ml, 1);
  mofs[ply + 1] = (uint16_t)(mofs[ply] + n);
  order(p, ml, sc, n, 0, ply);
  for (int i = 0; i < n; i++) {
    Move m = pick(ml, sc, n, i);
    if (G_QPRUNE(p, m, stand, alpha)) continue;
    if (SP_BADCAP && (SP_BADCAP >= 2 ? sc[i] < 0 : G_BADCAP(p, m))) continue;
    if (!make_move(p, &stk[ply + 1], m)) continue;
    int s = -qsearch(ply + 1, -beta, -alpha);
    if (stop) return 0;
    if (s >= beta) return s;
    if (s > alpha) alpha = s;
  }
  return alpha;
}

static void hist_add(int idx, int bonus) {
#if SP_HMAL
  int v = hh[idx];
  v += bonus - v * (bonus < 0 ? -bonus : bonus) / 16384;
  hh[idx] = (int16_t)(v > 16000 ? 16000 : v < -16000 ? -16000 : v);
#else
  int v = hh[idx] + bonus;
  hh[idx] = (int16_t)(v > 30000 ? 30000 : v);
#endif
}

static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

static int negamax(int ply, int depth, int alpha, int beta, int can_null) {
  Pos *p = &stk[ply];
  if ((++nodes & 1023) == 0) check_stop();
  if (stop) return 0;
  if (ply && (G_DRAWN(p) || is_rep(p))) return 0;
  if (ply >= MAXPLY - 2) return evaluate(p);
#if SP_MDP
  if (ply) {                                  /* a mate found nearer the root: nothing here can beat it */
    if (alpha < -MATE + ply) alpha = -MATE + ply;
    if (beta > MATE - ply - 1) beta = MATE - ply - 1;
    if (alpha >= beta) return alpha;
  }
#endif

  int incheck = in_check(p);
  if (incheck) depth++;
  if (depth <= 0) return qsearch(ply, alpha, beta);

  /* transposition table */
  TTE *e = tt_entry(p->key);
  Move ttm = 0;
  if (e->key == p->key) {
    ttm = e->move;
    if (ply && e->depth >= depth) {
      int s = score_from_tt(e->score, ply);
      int f = TT_BOUND(e);
      if (f == TT_EXACT) return s;
      if (f == TT_LOWER && s >= beta) return s;
      if (f == TT_UPPER && s <= alpha) return s;
    }
  }

  /* near the leaves, out of the principal variation: the static score decides what is worth searching */
  int pv = beta - alpha > 1;
  int st = incheck ? -INF : evaluate(p);
  if (SP_RFP && !pv && !incheck && ply && depth <= 3 && beta < MATE - 2 * MAXPLY && st - SP_RFP * depth >= beta)
    return st;                                /* reverse futility: far above beta, the opponent will not allow it */
  int fut = SP_FUT && !pv && !incheck && depth <= 2 && alpha > -MATE + 2 * MAXPLY && st + SP_FUT * depth <= alpha;

  /* null move pruning */
  if (can_null && !incheck && ply && depth >= 3 && beta < MATE - 2 * MAXPLY && G_HAS_PIECES(p) && st >= beta) {
    Pos *n = &stk[ply + 1];
    G_NULL_MOVE(p, n);
    rep[rep_n++] = n->key;
    mofs[ply + 1] = mofs[ply];                /* this node has no moves yet: the child's list starts here (a stale
                                                 offset from an earlier node could overwrite a parent's list) */
    int R = depth > 6 ? 3 : 2;
    int s = -negamax(ply + 1, depth - 1 - R, -beta, -beta + 1, 0);
    rep_n--;
    if (stop) return 0;
    if (s >= beta) return s >= MATE - 2 * MAXPLY ? beta : s;
  }

  if (mofs[ply] + MAXMOVES > MBUF) return evaluate(p);
  Move *ml = mbuf + mofs[ply];
  int32_t *sc = sbuf + mofs[ply];
  int n = gen_moves(p, ml, 0);
  mofs[ply + 1] = (uint16_t)(mofs[ply] + n);
  order(p, ml, sc, n, ttm, ply);

  int best = -INF, legal = 0, orig = alpha;
  Move bm = 0;
  for (int i = 0; i < n; i++) {
    Move m = pick(ml, sc, n, i);
    Pos *c = &stk[ply + 1];
    if (!make_move(p, c, m)) continue;
    legal++;
    rep[rep_n++] = c->key;
    int quiet = G_QUIET(m);
    int gives = in_check(c);
    /* futility and late move pruning: quiet moves that give no check, near the leaves, after the first */
    if (legal > 1 && quiet && !gives && !pv && !incheck && m != killer[ply][0] && m != killer[ply][1] &&
        (fut || (SP_LMP && depth <= 3 && legal > SP_LMP + 2 * depth * depth))) { rep_n--; continue; }
    /* root randomness for weak levels: shift this move's window by its noise
       so bounds stay exact (adding noise to a fail-low bound would be wrong) */
    int nz = 0;
    if (ply == 0 && L->noise)
      nz = (int)(hash32(m ^ L->seed) % (uint32_t)(2 * L->noise + 1)) - L->noise;
    int a = alpha - nz, bt = beta - nz;
    int s;
    if (legal == 1) {
      s = -negamax(ply + 1, depth - 1, -bt, -a, 1);
    } else {
      int r = 0;
#if SP_LMRT
      if (depth >= 3 && legal > 2 && quiet && !incheck && !gives) {
        r = LMR_T[depth > 31 ? 31 : depth][legal > 63 ? 63 : legal];
        if (pv) r--;
        if (m == killer[ply][0] || m == killer[ply][1]) r--;
        int hv = hh[G_HIDX(p, m)];
        r -= hv / 8192;
        if (r < 0) r = 0;
        if (r > depth - 2) r = depth - 2;
      }
#else
      if (depth >= 3 && legal > 3 && quiet && !incheck && !gives && m != killer[ply][0] && m != killer[ply][1])
        r = legal > 10 ? 2 : 1;
#endif
      s = -negamax(ply + 1, depth - 1 - r, -a - 1, -a, 1);
      if (s > a && r) s = -negamax(ply + 1, depth - 1, -a - 1, -a, 1);
      if (s > a && s < bt) s = -negamax(ply + 1, depth - 1, -bt, -a, 1);
    }
    rep_n--;
    if (stop) return 0;
    s += nz;
    if (s > best) {
      best = s;
      bm = m;
      if (ply == 0) { root_best = m; root_score = s; }
      if (s > alpha) {
        alpha = s;
        if (s >= beta) {
          if (quiet) {
            if (killer[ply][0] != m) { killer[ply][1] = killer[ply][0]; killer[ply][0] = m; }
            int bonus = depth * depth;
#if SP_HMAL
            bonus = depth > 13 ? 1600 : 8 * depth * depth + 16 * depth;
            for (int j = 0; j < i; j++)             /* the quiet moves tried before it (all of them are in ml[0..i)) */
              if (G_QUIET(ml[j])) hist_add(G_HIDX(p, ml[j]), -bonus);
#endif
            hist_add(G_HIDX(p, m), bonus);
          }
          break;
        }
      }
    }
  }
  if (!legal) return G_NOLEGAL(incheck, ply);

  if (pv && bm && best > orig && best < beta) {             /* a main-line node: its move kept for the display */
    PVE *q = &pvh[p->key & (PVH_N - 1)];                    /* the deeper kept (nearer the root), this search's */
    if (q->age != pv_age || q->key == p->key || depth >= q->depth) {
      q->key = p->key; q->move = bm; q->depth = (int8_t)depth; q->age = pv_age;
    }
  }
  e = tt_entry(p->key);                       /* again: the children may have stored here */
  if (e->key != p->key || depth >= e->depth || best >= beta) {
    e->key = p->key;
    e->move = bm;
    e->score = (int16_t)score_to_tt(best, ply);
    e->depth = (int8_t)depth;
    e->flag = (uint8_t)((best >= beta ? TT_LOWER : best <= orig ? TT_UPPER : TT_EXACT) | (SP_TT2 ? (pv_age & 63) << 2 : 0));
  }
  return best;
}

/* the expected line (the "thinking" display): first, then each position's main-line move (pvh, else the hash table),
   while the entry is that position's (the full key) and the move legal; at a repeated position it ends. The tables
   are only read: the search and its moves are not changed */
int PV_NAME(const Pos *root, Move first, Move *pv, int max) {
  Pos p = *root, n;
  uint64_t seen[16];
  int k = 0;
  Move m = first;
  while (m && k < max && k < 16) {
    if (!make_move(&p, &n, m)) break;
    for (int i = 0; i < k; i++) if (seen[i] == n.key) return k;
    seen[k] = n.key;
    pv[k++] = m;
    p = n;
    const PVE *q = &pvh[p.key & (PVH_N - 1)];
    const TTE *e = tt_entry(p.key);
    m = q->key == p.key ? q->move : e->key == p.key ? e->move : 0;
  }
  return k;
}

Result SEARCH_NAME(const Pos *root, const uint64_t *keys, int nkeys, const Limits *lim) {
  Result R;
  memset(&R, 0, sizeof R);
  L = lim;
  eval_hand = lim->hand_eval;
  stop = 0;
  nodes = 0;
  pv_age++;
  t0 = lim->now ? lim->now() : 0;

  int n = gen_legal(root, scratch);
  if (!n) return R;
  R.best = scratch[0];

  int first = nkeys > MAXGAME ? nkeys - MAXGAME : 0;
  rep_n = 0;
  for (int i = first; i < nkeys; i++) rep[rep_n++] = keys[i];
  if (!rep_n || rep[rep_n - 1] != root->key) rep[rep_n++] = root->key;

  for (int i = 0; i < G_HSIZE; i++) hh[i] >>= 2;
  memset(killer, 0, MAXPLY * 2 * sizeof(Move));
  stk[0] = *root;
  mofs[0] = 0;

  int maxd = lim->depth < 1 ? 1 : lim->depth > MAXPLY - 8 ? MAXPLY - 8 : lim->depth;
  for (int d = 1; d <= maxd; d++) {
    int s, a = -INF, b = INF, delta = SP_ASP;
    if (SP_ASP && d >= 5 && !IS_MATE(R.score)) { a = R.score - delta; b = R.score + delta; }
    for (;;) {
      root_best = 0;
      s = negamax(0, d, a, b, 1);
      if (stop) {                             /* a move proved better than the window's floor: take it */
        if (root_best && root_score > a) { R.best = root_best; R.score = root_score; R.depth = d; }
        break;
      }
      if (s <= a && a > -INF) {               /* failed low: the floor lowered */
        b = (a + b) / 2;
        a = s - delta < -INF ? -INF : s - delta;
      } else if (s >= b && b < INF) {         /* failed high: the move that did it kept, the ceiling raised */
        if (root_best) { R.best = root_best; R.score = s; }
        b = s + delta > INF ? INF : s + delta;
      } else {
        if (root_best) { R.best = root_best; R.score = s; R.depth = d; }
        break;
      }
      delta += delta;
      if (delta > 600) { a = -INF; b = INF; }
    }
    if (stop) break;
    if (lim->info) lim->info(d, R.score, nodes, R.best);
    if (n == 1 && d >= 2) break;
    if (IS_MATE(s) && d >= 6) break;
    if (lim->time_ms && lim->now && (uint32_t)(lim->now() - t0) > lim->time_ms / 2) break;
  }
  R.nodes = nodes;
  return R;
}
