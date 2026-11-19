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
 * and the tables: tt, tt_n, stk, mbuf, sbuf, mofs, killer, hh, rep, rep_n, scratch, nodes; MBUF; evaluate,
 * gen_moves, gen_legal, make_move, in_check.
 */

static int      stop;
static uint32_t t0;
static const Limits *L;
static Move     root_best;
static int      root_score;

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

static int score_to_tt(int s, int ply) { return s > MATE - 2 * MAXPLY ? s + ply : s < -MATE + 2 * MAXPLY ? s - ply : s; }
static int score_from_tt(int s, int ply) { return s > MATE - 2 * MAXPLY ? s - ply : s < -MATE + 2 * MAXPLY ? s + ply : s; }

static void order(const Pos *p, Move *ml, int32_t *sc, int n, Move ttm, int ply) {
  for (int i = 0; i < n; i++) {
    Move m = ml[i];
    int32_t v;
    if (m == ttm) v = 1 << 30;
    else if ((v = G_TACTICAL(p, m)) >= 0) ;
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
    if (!make_move(p, &stk[ply + 1], m)) continue;
    int s = -qsearch(ply + 1, -beta, -alpha);
    if (stop) return 0;
    if (s >= beta) return s;
    if (s > alpha) alpha = s;
  }
  return alpha;
}

static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

static int negamax(int ply, int depth, int alpha, int beta, int can_null) {
  Pos *p = &stk[ply];
  if ((++nodes & 1023) == 0) check_stop();
  if (stop) return 0;
  if (ply && (G_DRAWN(p) || is_rep(p))) return 0;
  if (ply >= MAXPLY - 2) return evaluate(p);

  int incheck = in_check(p);
  if (incheck) depth++;
  if (depth <= 0) return qsearch(ply, alpha, beta);

  /* transposition table */
  TTE *e = &tt[p->key & (tt_n - 1)];
  Move ttm = 0;
  if (e->key == p->key) {
    ttm = e->move;
    if (ply && e->depth >= depth) {
      int s = score_from_tt(e->score, ply);
      if (e->flag == TT_EXACT) return s;
      if (e->flag == TT_LOWER && s >= beta) return s;
      if (e->flag == TT_UPPER && s <= alpha) return s;
    }
  }

  /* null move pruning */
  if (can_null && !incheck && ply && depth >= 3 && beta < MATE - 2 * MAXPLY && G_HAS_PIECES(p) && evaluate(p) >= beta) {
    Pos *n = &stk[ply + 1];
    G_NULL_MOVE(p, n);
    rep[rep_n++] = n->key;
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
      if (depth >= 3 && legal > 3 && quiet && !incheck && !gives && m != killer[ply][0] && m != killer[ply][1])
        r = legal > 10 ? 2 : 1;
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
            int idx = G_HIDX(p, m);
            int v = hh[idx] + depth * depth;
            hh[idx] = (int16_t)(v > 30000 ? 30000 : v);
          }
          break;
        }
      }
    }
  }
  if (!legal) return G_NOLEGAL(incheck, ply);

  if (e->key != p->key || depth >= e->depth || best >= beta) {
    e->key = p->key;
    e->move = bm;
    e->score = (int16_t)score_to_tt(best, ply);
    e->depth = (int8_t)depth;
    e->flag = best >= beta ? TT_LOWER : best <= orig ? TT_UPPER : TT_EXACT;
  }
  return best;
}

Result SEARCH_NAME(const Pos *root, const uint64_t *keys, int nkeys, const Limits *lim) {
  Result R;
  memset(&R, 0, sizeof R);
  L = lim;
  stop = 0;
  nodes = 0;
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
    root_best = 0;
    int s = negamax(0, d, -INF, INF, 1);
    if (root_best) { R.best = root_best; R.score = stop ? root_score : s; R.depth = d; }
    if (stop) break;
    if (lim->info) lim->info(d, R.score, nodes, R.best);
    if (n == 1 && d >= 2) break;
    if (IS_MATE(s) && d >= 6) break;
    if (lim->time_ms && lim->now && (uint32_t)(lim->now() - t0) > lim->time_ms / 2) break;
  }
  R.nodes = nodes;
  return R;
}
