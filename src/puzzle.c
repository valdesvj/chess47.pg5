/*
 * CHESS47 for DM42n (DMCP5) - Puzzles 47 (SHIFT 4): 47 chess problems in the public domain, as a newspaper printed
 * them: "White to play and mate in 3", "Black to play and win", "White to play and draw" (src/puzzles.h)
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 *
 * Included by main.c (it shares the board, the moves, the panel and the main loop). The game being played is put
 * aside (serialize, as SAVE writes it) and comes back as it was at EXIT, its clock waiting meanwhile.
 * Your moves are checked:
 *   mate in n   the solution's move at once; another move if it still mates in the moves left, proven by the exact
 *               mate solver below (every defence tried); a long proof gives way to the search's mate score
 *   win / draw  the solution's move at once; another move if a short search finds it keeps the result
 * The defence: the solution's move while you follow it, else the search's best move (the longest resistance).
 */
#include "puzzles.h"

static char    *pz_aside;          /* the game put aside (serialize's text); NULL until the first SHIFT 4 */
static Cfg      pz_cfg;            /* its settings */
static int8_t   pz_clk_off, pz_from_xq, pz_over, pz_resigner;
static int      pz_i;              /* the puzzle shown, 0..46 */
static uint8_t  pz_solved[(PUZZLE_CNT + 7) / 8];
static int8_t   pz_loaded;         /* the progress file read */
static int      pz_checked;        /* the plies already checked (G->n) */
static int8_t   pz_done;           /* solved (1) */
static int8_t   pz_hint;           /* HINT pressed: 1 the piece shown, 2 the move */

#define PZ_FILE  DIR "/PUZZLES.TXT"
#define PZ_CAP   400000u           /* the mate solver's positions before it gives way to the search */

static const Puzzle *PZ(void) { return &PUZZLES[pz_i]; }
static int pz_is_solved(int i) { return pz_solved[i >> 3] >> (i & 7) & 1; }
static int pz_count(void) { int c = 0; for (int i = 0; i < PUZZLE_CNT; i++) c += pz_is_solved(i); return c; }

/* the solution's ply k as a move in the position before it (0: past the line's end or not legal there) */
static Move pz_line_move(int k, const Pos *p) {
  const char *s = PZ()->line;
  for (int i = 0; i < k && *s; i++) { while (*s && *s != ' ') s++; while (*s == ' ') s++; }
  if (!*s) return 0;
  char mv[8]; int n = 0;
  while (s[n] && s[n] != ' ' && n < 6) { mv[n] = s[n]; n++; }
  mv[n] = 0;
  return str_to_move(p, mv);
}
static int pz_line_len(void) {
  int n = 0;
  for (const char *s = PZ()->line; *s; ) { n++; while (*s && *s != ' ') s++; while (*s == ' ') s++; }
  return n;
}
/* the moves played so far are the solution's */
static int pz_on_line(void) {
  for (int k = 0; k < G->n; k++) if (pz_line_move(k, &G->hist[k]) != G->moves[k]) return 0;
  return 1;
}

static void pz_stip(char *s) {
  const Puzzle *z = PZ();
  const char *side = G->hist[0].side ? "Black" : "White";
  if (z->kind == PZ_MATE) sprintf(s, "%s to play and mate in %d", side, z->n);
  else sprintf(s, "%s to play and %s", side, z->kind == PZ_WIN ? "win" : "draw");
}

/* ---- progress: /CHESS47/PUZZLES.TXT, "PUZZLES47 1", the puzzle shown, one 0/1 for each ---- */
static void pz_save(void) {
  char s[16 + PUZZLE_CNT + 8];
  int n = sprintf(s, "PUZZLES47 1\n%d\n", pz_i + 1);
  for (int i = 0; i < PUZZLE_CNT; i++) s[n++] = (char)('0' + pz_is_solved(i));
  s[n++] = '\n'; s[n] = 0;
  write_file(PZ_FILE, s);
}
static void pz_load_progress(void) {
  char s[16 + PUZZLE_CNT + 8];
  pz_loaded = 1;
  if (!read_file(PZ_FILE, s, sizeof s) || strncmp(s, "PUZZLES47", 9)) return;
  char *l = strchr(s, '\n');
  if (!l) return;
  int i = atoi(l + 1);
  if (i >= 1 && i <= PUZZLE_CNT) pz_i = i - 1;
  l = strchr(l + 1, '\n');
  if (!l) return;
  l++;
  for (int k = 0; k < PUZZLE_CNT && (l[k] == '0' || l[k] == '1'); k++)
    if (l[k] == '1') pz_solved[k >> 3] |= (uint8_t)(1 << (k & 7));
}

/* ---- the exact mate solver: the moves on the engine's move stack (no search runs meanwhile) ---- */
static uint32_t pz_nodes;
static int8_t   pz_abort;

static int pz_no_legal(const Pos *p, Move *buf) {
  int n = gen_moves(p, buf, 0);
  Pos c;
  for (int i = 0; i < n; i++) if (make_move(p, &c, buf[i])) return 0;
  return 1;
}

static int pz_def(const Pos *p, int n, Move *buf);
/* the side to move mates in n or fewer moves (checks tried first) */
static int pz_att(const Pos *p, int n, Move *buf) {
  int cnt = gen_moves(p, buf, 0);
  Pos c;
  for (int pass = 0; pass < (n > 1 ? 2 : 1); pass++)
    for (int i = 0; i < cnt; i++) {
      if (!make_move(p, &c, buf[i])) continue;
      int chk = in_check(&c);
      if (chk != (pass == 0)) continue;
      if (++pz_nodes > PZ_CAP) { pz_abort = 1; return 0; }
      if (chk && pz_no_legal(&c, buf + cnt)) return 1;
      if (n > 1 && pz_def(&c, n - 1, buf + cnt)) return 1;
      if (pz_abort) return 0;
    }
  return 0;
}
/* every move of the side to move allows mate in n (no move: mated yes, stalemate no) */
static int pz_def(const Pos *p, int n, Move *buf) {
  int cnt = gen_moves(p, buf, 0), legal = 0;
  Pos c;
  for (int i = 0; i < cnt; i++) {
    if (!make_move(p, &c, buf[i])) continue;
    legal++;
    if (++pz_nodes > PZ_CAP) { pz_abort = 1; return 0; }
    if (!pz_att(&c, n, buf + cnt)) return 0;
  }
  return legal ? 1 : in_check(p);
}

/* ---- the puzzle on the board ---- */
static void pz_start(int i) {
  Pos p;
  pz_i = (i + PUZZLE_CNT) % PUZZLE_CNT;
  chess960 = 0;
  if (!pos_from_fen(&p, PZ()->fen)) pos_start(&p);
  cfg.mode = MODE_HC; cfg.human = (int8_t)p.side; cfg.flip = (int8_t)p.side; cfg.tc = 0;
  new_game_from(&p, -1);
  pz_checked = 0; pz_done = 0; pz_hint = 0; pz_inv = 0;
}

static void pz_enter(void) {
  if (!xq && VS_USB) { strcpy(status, "No puzzles vs USB"); return; }   /* the PC (Lichess) follows the board */
  if (xq) { game_switch(); if (xq) return; pz_from_xq = 1; }          /* Xiangqi's game and clock kept */
  else pz_from_xq = 0;
  if (!pz_aside && !(pz_aside = malloc(sizeof G->fbuf))) { strcpy(status, "No memory for puzzles"); return; }
  if (clk_live) { clk[side_now()] = clock_left(side_now()); clk_live = 0; }
  serialize(pz_aside);
  pz_cfg = cfg; pz_clk_off = clk_off; pz_over = (int8_t)G->over; pz_resigner = resigner;
  if (!pz_loaded) pz_load_progress();
  pz = 1;
  zoom = 0;
  pz_start(pz_i);
}

static void pz_exit(void) {
  pz = 0;
  memcpy(G->fbuf, pz_aside, sizeof G->fbuf);
  int snd = cfg.sound;
  cfg.sound = 0;                                        /* the moves replayed without their clicks */
  if (!deserialize(G->fbuf)) new_game(-1);
  cfg = pz_cfg;
  cfg.sound = (int8_t)snd;
  clk_off = pz_clk_off;
  if (pz_over >= 6) { G->over = pz_over; resigner = pz_resigner; clk_live = 0; }   /* flag, resign, draw */
  clock_update_live();
  pz_save();
  strcpy(status, "Game resumed");
  if (pz_from_xq) game_switch();
}

/* power off in puzzles: the game put aside is the one kept (AUTO.TXT) */
static void pz_poweroff(void) {
  write_file(DIR "/AUTO.TXT", pz_aside);
  pz_save();
}

static void pz_solved_now(void) {
  pz_done = 1;
  pz_solved[pz_i >> 3] |= (uint8_t)(1 << (pz_i & 7));
  sprintf(status, "Solved! %d of %d", pz_count(), PUZZLE_CNT);
  pz_inv = 1;
  pz_save();
  beep(1320, 120);
}

static void pz_wrong(const char *why) {
  G->n--;                                               /* the move taken back */
  G->over = 0;
  last_from = last_to = NOSQ;
  if (G->n) { last_from = MFROM(G->moves[G->n - 1]); last_to = MTO(G->moves[G->n - 1]); }
  refresh_legal();
  pz_checked = G->n;
  strcpy(status, why);
  pz_inv = 1;
  beep(220, 200);
}

/* the score of the position after the solver's move, from the solver's side (a short search) */
static int pz_search_score(uint32_t ms) {
  Result r = run_search(40, ms, 0);
  return -r.score;                                      /* the search scores for the side to move: the defence */
}

/* the main loop's turn in puzzles: the solver's last move checked, then the defence. 1 if something changed */
static int pz_turn(void) {
  if (G->n < pz_checked) pz_checked = G->n;             /* taken back */
  if (pz_done || G->n == pz_checked) return 0;
  int solver = G->hist[0].side;
  if (CUR->side == solver) { pz_checked = G->n; return 0; }   /* the defence's move: nothing to check */
  const Puzzle *z = PZ();
  int k = (G->n - 1) / 2;                               /* the solver's move number, 0 = the key */
  int online = pz_on_line();
  pz_checked = G->n;
  pz_inv = 0;
  if (z->kind == PZ_MATE) {
    int left = z->n - k - 1;                            /* the moves left after this one */
    if (G->over == 1) { pz_solved_now(); return 1; }
    if (G->over || left < 1) { pz_wrong(left < 1 ? "No mate: try again" : "Not the solution"); return 1; }
    if (!online) {
      strcpy(status, "Checking..."); redraw();
      pz_nodes = 0; pz_abort = 0;
      int ok = pz_def(CUR, left, eng_shared()->mbuf);
      if (pz_abort) {                                   /* too long to prove: the search's mate score */
        int s = pz_search_score(3000);
        ok = s > MATE - 2 * MAXPLY && (MATE - s + 1) / 2 <= left;
      }
      if (!ok) { pz_wrong("Not the solution"); return 1; }
    }
  } else {
    int need = (pz_line_len() + 1) / 2;                 /* the solver's moves in the stored line */
    if (G->over == 1) { pz_solved_now(); return 1; }    /* mated */
    if (G->over) {                                      /* a draw by rule: a draw solves a draw, loses a win */
      if (z->kind == PZ_DRAW) { pz_solved_now(); return 1; }
      pz_wrong("A draw: try again"); return 1;
    }
    if (!online) {
      strcpy(status, "Checking..."); redraw();
      int s = pz_search_score(3000);
      if (z->kind == PZ_WIN ? s < 250 : s <= -100) { pz_wrong("Not the solution"); return 1; }
    }
    if (k + 1 >= need) { pz_solved_now(); return 1; }
  }
  /* the defence */
  Move m = online ? pz_line_move(G->n, CUR) : 0;
  if (!m) { Result r = run_search(40, 1500, 0); m = r.best ? r.best : G->legal[0]; }
  sys_delay(250);
  do_move(m);
  pz_checked = G->n;
  if (G->over) {                                        /* the defence ended it: stalemate or a draw by rule */
    if (z->kind == PZ_DRAW) pz_solved_now();
    else { strcpy(status, G->over == 1 ? "Mated: RETRY" : "A draw: RETRY"); pz_inv = 1; }
  } else strcpy(status, "Your move");
  return 1;
}

/* HINT: first the piece to move, then the move (the solution's, or the search's off the line) */
static void pz_hint_key(void) {
  if (pz_done || G->over || CUR->side != G->hist[0].side) return;
  Move m = pz_on_line() ? pz_line_move(G->n, CUR) : 0;
  if (!m) { Result r = run_search(40, 1500, 0); m = r.best; }
  if (!m) return;
  pz_hint = (int8_t)(pz_hint >= 1 ? 2 : 1);
  hint_from = MFROM(m);
  hint_to = pz_hint == 2 ? ((MFLAGS(m) & F_CASTLE) ? castle_rook_sq(CUR, m) : MTO(m)) : NOSQ;
  if (pz_hint == 2) { char san[10]; move_to_san(CUR, m, san); sprintf(status, "Hint: %s", san); }
  else strcpy(status, "Hint: this piece");
  pz_inv = 0;
}

/* SOLVE: the solution played from the start, a move at a time */
static void pz_solve(void) {
  pz_start(pz_i);
  int n = pz_line_len();
  strcpy(status, "The solution");
  redraw();
  for (int k = 0; k < n; k++) {
    sys_delay(700);
    Move m = pz_line_move(k, CUR);
    if (!m) break;
    do_move(m);
    strcpy(status, "The solution");
    redraw();
  }
  pz_checked = G->n;
  pz_done = 2;                                          /* shown, not solved */
}

static void pz_back(void) {                             /* <-: your last move taken back (with the defence's reply) */
  if (!G->n || pz_done == 1) return;
  int plies = CUR->side == G->hist[0].side && G->n >= 2 ? 2 : 1;
  G->n -= plies;
  G->over = 0;
  pz_done = 0;
  last_from = last_to = sel = hint_from = hint_to = NOSQ;
  if (G->n) { last_from = MFROM(G->moves[G->n - 1]); last_to = MTO(G->moves[G->n - 1]); }
  refresh_legal();
  pz_checked = G->n;
  status[0] = 0; pz_inv = 0;
}

static const char *PZ_MENU[6] = { "PREV", "NEXT", "HINT", "SOLVE", "RETRY", "EXIT" };
static void pz_menu(void) { lcd_draw_menu_keys(PZ_MENU); }

static void pz_softkey(int fn) {
  switch (fn) {
  case 0: pz_start(pz_i - 1); break;
  case 1: pz_start(pz_i + 1); break;
  case 2: pz_hint_key(); break;
  case 3: pz_solve(); break;
  case 4: pz_start(pz_i); break;
  case 5: pz_exit(); break;
  }
}

/* ---- the panel: the header (instead of the clock), the title and the stipulation (instead of the level) ---- */
static void pz_draw_head(void) {
  int h = lh(t20) + 2;
  char s[32];
  lcd_fill_rect(0, 0, PANEL_W + 4, h, 0);
  ptext(2, 1, "PUZZLES 47", 1);
  sprintf(s, "%s%d/%d", pz_is_solved(pz_i) ? "* " : "", pz_i + 1, PUZZLE_CNT);
  ptext(PANEL_W - lcd_textWidth(t20, s), 1, s, 0);
}

/* a text wrapped at the words to the panel's width, max lines at most; returns the y below it */
static int pz_wrap(int y, const char *txt, int max) {
  int h = lh(t20), w = PANEL_W - 4, ln = 0;
  char line[48] = "", t[48], word[48];
  const char *p = txt;
  while (*p && ln < max) {
    int n = 0;
    while (*p == ' ') p++;
    while (*p && *p != ' ' && n < 47) word[n++] = *p++;
    word[n] = 0;
    if (!n) break;
    snprintf(t, sizeof t, "%s%s%s", line, line[0] ? " " : "", word);
    if (line[0] && lcd_textWidth(t20, t) > w) { ptext(2, y, line, 0); y += h; ln++; strcpy(line, word); }
    else strcpy(line, t);
  }
  if (line[0] && ln < max) { ptext(2, y, line, 0); y += h; }
  return y;
}

static int pz_draw_lines(int y) {
  char s[48];
  y = pz_wrap(y, PZ()->title, 3);
  pz_stip(s);
  return pz_wrap(y, s, 2);
}
