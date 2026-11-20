/*
 * CHESS47 for DM42n (DMCP5) - the Xiangqi game: its screen, keys and moves.
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 *
 * Included by main.c (it shares the clock, the levels, the modes, the sound, the zoom and the main loop): SHIFT F1
 * (or SHIFT, then F1 XIANGQI / CHESS on the game menu) switches between chess and Xiangqi. As chess: normal, the
 * board on the right above the soft keys, the points 21 px apart, the pieces 19 px; zoom (+ / -), the full height,
 * 24 px apart, 23 px pieces, no soft keys (they still work; SHIFT shows the game menu). The round pieces of
 * src/xq_pieces.h: the traditional characters (Red 帥仕相傌俥炮兵, Black 將士象馬車砲卒), symbols (crown, shield,
 * elephant, horse, chariot, cannon, soldier: Red grey, Black white) or the WXF letters (x or SHIFT F2 PIECES).
 * The panel on the left: the clock, the level, the state, the moves in WXF notation.
 * The Easter egg, SHIFT 7 (七, seven): 七星聚會, the Seven Stars Gathering, the king of the classical endgames
 * (竹香齋 1801: each side seven pieces, Red to move; with the best play a draw).
 * Not yet for Xiangqi: the USB opponent, SAVE / LOAD / PGN (chess's game is kept while Xiangqi is played).
 */
#include "xq_engine.h"
#include "xq_pieces.h"

typedef struct {
  XPos     start, cur;
  uint64_t keys[MAXGAME + 1];
  Move     moves[MAXGAME];
  char     wxf[MAXGAME][8];
  int      n, over;
  Move     legal[MAXMOVES];
  int      nlegal;
} XGame;
static XGame *XG;
static int8_t xstyle;                                   /* the key x: 0 characters, 1 symbols, 2 letters */
static int8_t xseven;                                   /* the game started from the Seven Stars (SHIFT 7) */
static int8_t xresigner = -1;                           /* the side that resigned (XG->over 7) */
static int  xcpu_score;                                 /* the CPU's last search score, its own view (DRAW) */
static int xcur, xsel = -1, xlast_from = -1, xlast_to = -1, xhint_from = -1, xhint_to = -1;

/* normal: 21 px between the points, 19 px pieces: the board x 213 .. 399, y 0 .. 207 (the soft keys from 208);
   zoom: 24 px, 23 px pieces, x 185 .. 399, y 0 .. 239 */
#define XGAP   (zoom ? 24 : 21)              /* between two points */
#define XB0    (zoom ? 196 : 222)            /* file a's x */
#define XY0    (zoom ? 11 : 9)               /* rank 9's y */
#define XPANEL (zoom ? 182 : 209)
#define XR     (zoom ? 12 : 11)              /* the marks' corners: in the gap between two pieces */
#define XBOT   (zoom ? LCD_Y : LCD_Y - LCD_MENU_LINES)

static int xpx(int f) { return XB0 + (cfg.flip ? 8 - f : f) * XGAP; }
static int xpy(int r) { return XY0 + (cfg.flip ? r : 9 - r) * XGAP; }

static int xq_side(void) { return XG->cur.side; }
static int xq_over_n(int *n) { *n = XG->n; return XG->over; }
static int xq_first_side(void) { return XG->start.side; }

static void xq_refresh_legal(void) { XG->nlegal = xq_gen_legal(&XG->cur, XG->legal); }

static void xq_new_game_at(const char *fen) {
  if (!fen || !xq_from_fen(&XG->start, fen)) xq_pos_start(&XG->start);
  XG->cur = XG->start;
  XG->keys[0] = XG->cur.key;
  XG->n = XG->over = 0;
  xcpu_score = 0; offer = -1;
  xsel = xhint_from = xhint_to = xlast_from = xlast_to = -1;
  xcur = XSQ(7, 2);                                     /* Red's cannon: C2=5 is the most common first move */
  status[0] = 0;
  clk_off = 0;
  clock_reset();
  eng_clear_tt();
  xq_refresh_legal();
  xseven = 0;
}
static void xq_new_game(void) { xq_new_game_at(0); }

/* SHIFT 7: 七星聚會, as 竹香齋 sets it (the river-line soldier): Red 帥 e1, 俥 g1 h1, 炮 h3, 兵 d9 f8 i5; Black 將 f10,
   車 e10, 象 e8, 卒 b3 e3 d2 f2. The famous line: 炮二平四 卒5平6 兵四進一 將6進1 俥三進八 ... */
static void xq_seven(void) {
  xq_new_game_at("4rk3/3P5/4bP3/9/9/8P/9/1p2p2C1/3p1p3/4K1RR1 w - - 0 1");
  xseven = 1;
  strcpy(status, "Seven Stars 1801");
  beep(784, 120); beep(988, 120); beep(1175, 120); beep(1568, 300);
}

static int xq_enter(void) {                             /* 1: Xiangqi ready */
  if (!XG) {
    XG = malloc(sizeof(XGame));
    if (!XG || !xq_init(heap_alloc)) { free(XG); XG = 0; strcpy(status, "No memory for Xiangqi"); return 0; }
    xq_new_game();
  }
  eng_clear_tt();
  return 1;
}

/* ------------------------------------------------------------------ */
/*  Drawing                                                           */
/* ------------------------------------------------------------------ */
static void xline(int x0, int y0, int x1, int y1) {     /* horizontal, vertical or 45 degrees */
  int dx = x1 > x0 ? 1 : x1 < x0 ? -1 : 0, dy = y1 > y0 ? 1 : y1 < y0 ? -1 : 0;
  if (!dy) { lcd_fill_rect(dx > 0 ? x0 : x1, y0, (dx > 0 ? x1 - x0 : x0 - x1) + 1, 1, 1); return; }
  if (!dx) { lcd_fill_rect(x0, dy > 0 ? y0 : y1, 1, (dy > 0 ? y1 - y0 : y0 - y1) + 1, 1); return; }
  for (int x = x0, y = y0;; x += dx, y += dy) { lcd_fill_rect(x, y, 1, 1, 1); if (x == x1) break; }
}

static void xq_grid(void) {
  for (int r = 0; r < 10; r++) xline(xpx(0), xpy(r), xpx(8), xpy(r));
  for (int f = 0; f < 9; f++) {
    if (f == 0 || f == 8) xline(xpx(f), xpy(0), xpx(f), xpy(9));
    else { xline(xpx(f), xpy(0), xpx(f), xpy(4)); xline(xpx(f), xpy(5), xpx(f), xpy(9)); }
  }
  for (int r0 = 0; r0 <= 7; r0 += 7) {                  /* the palaces' diagonals */
    xline(xpx(3), xpy(r0), xpx(5), xpy(r0 + 2));
    xline(xpx(5), xpy(r0), xpx(3), xpy(r0 + 2));
  }
  /* the river: 楚河 (Black's side, left) 漢界 (right), between ranks 4 and 5 */
  int ym = (xpy(4) + xpy(5)) / 2 - XQ_RSIZE / 2, step = zoom ? 22 : 19;
  for (int k = 0; k < 4; k++) {                         /* each pair centred on file b / h's middle (c, g) */
    int x0 = XB0 + (k < 2 ? 2 : 6) * XGAP - (step + XQ_RSIZE) / 2 + (k & 1) * step;
    for (int y = 0; y < XQ_RSIZE; y++) {
      int row = XQ_RIVER[k][2 * y] << 8 | XQ_RIVER[k][2 * y + 1];
      for (int x = 0; x < XQ_RSIZE; x++) if (row >> (15 - x) & 1) lcd_fill_rect(x0 + x, ym + y, 1, 1, 1);
    }
  }
}

static int xq_is_target(int sq) {
  if (xsel < 0) return 0;
  for (int i = 0; i < XG->nlegal; i++) if (XFROM(XG->legal[i]) == xsel && XTO(XG->legal[i]) == sq) return 1;
  return 0;
}

/* a piece: the disc's pixels from its bitmap (inverted when picked); outside the disc the grid stays */
static void xq_piece(int pc, int cx, int cy, int inv) {
  int n = zoom ? XQ_PSIZE : XQ_PSIZE_N;
  const uint8_t *bm = zoom ? XQ_PIECE[xstyle][XCOLOR(pc)][XTYPE(pc) - 1] : XQ_PIECE_N[xstyle][XCOLOR(pc)][XTYPE(pc) - 1];
  const uint8_t *disc = zoom ? XQ_DISC : XQ_DISC_N;
  int x0 = cx - n / 2, y0 = cy - n / 2;
  for (int y = 0; y < n; y++) {
    uint32_t ink = (uint32_t)bm[3 * y] << 16 | bm[3 * y + 1] << 8 | bm[3 * y + 2];
    uint32_t dsc = (uint32_t)disc[3 * y] << 16 | disc[3 * y + 1] << 8 | disc[3 * y + 2];
    for (int x = 0; x < n; ) {                          /* runs of one value */
      if (!(dsc >> (23 - x) & 1)) { x++; continue; }
      int v = (int)(ink >> (23 - x) & 1) ^ inv, x1 = x;
      while (x1 < n && (dsc >> (23 - x1) & 1) && ((int)(ink >> (23 - x1) & 1) ^ inv) == v) x1++;
      lcd_fill_rect(x0 + x, y0 + y, x1 - x, 1, v);
      x = x1;
    }
  }
}

static void xq_frame(int cx, int cy, int r, int w) {    /* a square frame round a point */
  lcd_fill_rect(cx - r, cy - r, 2 * r + 1, w, 1);
  lcd_fill_rect(cx - r, cy + r - w + 1, 2 * r + 1, w, 1);
  lcd_fill_rect(cx - r, cy - r, w, 2 * r + 1, 1);
  lcd_fill_rect(cx + r - w + 1, cy - r, w, 2 * r + 1, 1);
}

/* four corners at r from a point, each len long and w thick: they stay in the gap between the pieces (zoom: a disc
   ends 11 from its point, the next starts 13 away; normal: 9 and 12), so nothing next to it is drawn over */
static void xq_corners(int cx, int cy, int r, int len, int w) {
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2) {
      int x = sx < 0 ? cx - r : cx + r - w + 1, y = sy < 0 ? cy - r : cy + r - w + 1;
      lcd_fill_rect(sx < 0 ? x : cx + r - len + 1, y, len, w, 1);      /* along x */
      lcd_fill_rect(x, sy < 0 ? y : cy + r - len + 1, w, len, 1);      /* along y */
    }
}

static void xq_draw_board(void) {
  lcd_fill_rect(XPANEL + 2, 0, LCD_X - XPANEL - 2, XBOT, 0);
  xq_grid();
  for (int r = 0; r < 10; r++)
    for (int f = 0; f < 9; f++) {
      int s = XSQ(f, r), pc = XG->cur.b[s], cx = xpx(f), cy = xpy(r);
      if (pc) xq_piece(pc, cx, cy, s == xsel);
      if (xq_is_target(s)) {
        if (pc) xq_frame(cx, cy, XR, 1);
        else lcd_fill_rect(cx - 2, cy - 2, 5, 5, 1);
      }
      if (s == xlast_from || s == xlast_to || s == xhint_from || s == xhint_to) xq_corners(cx, cy, XR, 4, 1);
      if (s == xcur && !thinking) xq_corners(cx, cy, XR, zoom ? 7 : 6, 2);   /* the cursor: thick corners, in the gap */
    }
}

static void xptext(int x, int y, const char *s, int inv) {
  char b[40];
  snprintf(b, sizeof b, "%s", s);
  for (size_t n = strlen(b); n && x + lcd_textWidth(t20, b) > XPANEL; ) b[--n] = 0;
  if (inv && b[0]) lcd_fill_rect((uint32_t)(x > 2 ? x - 2 : 0), (uint32_t)y, (uint32_t)lcd_textWidth(t20, b) + 4,
                                 (uint32_t)lcd_lineHeight(t20), 1);
  text_at(t20, x, y, b, inv);
}

static const char *xq_result(int code, int loser) {
  switch (code) {
  case 1: return loser == XRED ? "Checkmate  0-1" : "Checkmate  1-0";
  case 2: return loser == XRED ? "No moves  0-1" : "No moves  1-0";       /* stalemate loses in Xiangqi */
  case 3: return "60 moves  1/2";
  case 4: return "No attackers 1/2";
  case 5: return "Repetition 1/2";
  case 6: return loser == XRED ? "Red flag fell 0-1" : "Black flag 1-0";
  case 7: return xresigner == XRED ? "Red resigns 0-1" : "Black resigns 1-0";
  case 8: return "Draw agreed 1/2";
  }
  return "";
}

static void xq_draw_clock(void) {
  int h = lh(t20) + 2;
  lcd_fill_rect(0, 0, XPANEL + 1, h, 0);
  if (!TC && !XG->n) { xptext(2, 1, "XIANGQI 47", 1); return; }   /* OFF: the title, then the time used */
  char a[16], b[16], s[24];
  fmt_clock(clock_left(XRED), a);
  fmt_clock(clock_left(XBLACK), b);
  clock_label(s, 'R', a, XPANEL / 2 - 1);
  xptext(0, 1, s, XG->cur.side == XRED && !XG->over);
  clock_label(s, 'B', b, XPANEL / 2 - 1);
  xptext(XPANEL / 2 + 2, 1, s, XG->cur.side == XBLACK && !XG->over);
  lcd_fill_rect(XPANEL / 2, 0, 1, h, 1);
}

static void xq_draw_panel(void) {
  char s[48];
  int h = lh(t20), y;
  lcd_fill_rect(0, 0, XPANEL + 1, XBOT, 0);
  xq_draw_clock();
  y = h + 4;
  char lv[8];
  sprintf(lv, cfg.level ? "L%s" : "%s", LEVELS[cfg.level].name);
  if (cfg.mode == MODE_HH) strcpy(s, "2 players");
  else if (cfg.mode == MODE_CC) sprintf(s, "Demo  %s", lv);
  else sprintf(s, "%s  You:%s", lv, cfg.human ? "Black" : "Red");
  xptext(2, y, s, 0);
  y += h;
  int inv = 0;
  if (XG->over) { strcpy(s, xq_result(XG->over, XG->cur.side)); inv = 1; }
  else if (xq_in_check(&XG->cur)) { sprintf(s, "CHECK! %s", XG->cur.side ? "Black" : "Red"); inv = 1; }
  else if (cfg.mode == MODE_HH && offer == (XG->cur.side ^ 1)) sprintf(s, "%s offers a draw", offer ? "Black" : "Red");
  else if (status[0]) snprintf(s, sizeof s, "%s", status);
  else sprintf(s, "%s to move", XG->cur.side ? "Black" : "Red");
  xptext(2, y, s, inv);
  y += h + 2;
  lcd_fill_rect(2, y, XPANEL - 4, 1, 1);
  y += 3;
  if (xseven) {                                         /* the Seven Stars: its name, 七星聚會 */
    for (int k = 0; k < 4; k++)
      for (int yy = 0; yy < XQ_RSIZE; yy++) {
        int row = XQ_SEVEN[k][2 * yy] << 8 | XQ_SEVEN[k][2 * yy + 1];
        for (int x = 0; x < XQ_RSIZE; x++) if (row >> (15 - x) & 1) lcd_fill_rect(4 + k * 18 + x, y + 1 + yy, 1, 1, 1);
      }
    y += XQ_RSIZE + 3;
  }
  if (thinking && show_think && think_n) {              /* the CPU's thinking: the expected line (WXF), the counts */
    char wd[12][10];
    XPos p = XG->cur, q;
    for (int i = 0; i < think_n; i++) { xq_move_to_wxf(&p, think_pv[i], wd[i]); xq_make_move(&p, &q, think_pv[i]); p = q; }
    y = draw_think(y, XPANEL - 4, wd, think_n, xptext);
  }
  /* the moves, two a line (WXF), the last that fit */
  int bottom = cfg.level == 0 && cfg.mode != MODE_HH ? XBOT - 50 : XBOT - 2, lines = (bottom - y) / h;
  int total = (XG->n + 1) / 2, first = total - lines;
  if (first < 0) first = 0;
  for (int ln = first; ln < total; ln++) {
    sprintf(s, "%d.", ln + 1);
    xptext(2, y, s, 0);
    xptext(46, y, XG->wxf[2 * ln], 0);                 /* room for 3-digit move numbers (Xiangqi games run long) */
    if (2 * ln + 1 < XG->n) xptext(114, y, XG->wxf[2 * ln + 1], 0);
    y += h;
  }
  if (cfg.level == 0 && cfg.mode != MODE_HH) {          /* CRAZY: the crazy face, as chess */
    lcd_fill_rect(0, XBOT - 48, XPANEL + 1, 48, 0);
    draw_bitmap(CRAZY_BMP, CRAZY_W, CRAZY_H, 4, XBOT - 46, 2);
    xptext(60, XBOT - 36, "CRAZY!", 1);
  }
}

/* the soft keys, as chess's: the pages, and SHIFT the game menu (drawn in the zoom too, over the board's foot) */
static const char *XQ_MENU[3][6] = {
  { "NEW", "LEVEL", "FLIP", "UNDO", "HINT", ">>" },
  { "PIECES", "CLOCK", "SIDE", "", "", ">>" },
  { "CHESS", "MODE", "", "", "INFO", ">>" },
};

static void xq_draw_menu(void) {
  if (shift) { game_menu(); return; }
  if (zoom) return;
  lcd_draw_menu_keys(XQ_MENU[cfg.page]);
}

static void xq_redraw(void) {
  lcd_clear_buf();
  xq_draw_board();
  xq_draw_panel();
  xq_draw_menu();
  lcd_show();
  if (cfg.usb) usbview_send();
}

/* ------------------------------------------------------------------ */
/*  Moves, the computer                                               */
/* ------------------------------------------------------------------ */
static void xq_do_move(Move m) {
  if (XG->n >= MAXGAME) { strcpy(status, "Game too long"); return; }
  int mover = XG->cur.side;
  if (clock_move(mover)) { XG->over = 6; return; }
  turn_start = now_ms();
  xq_move_to_wxf(&XG->cur, m, XG->wxf[XG->n]);
  XPos n;
  xq_make_move(&XG->cur, &n, m);
  XG->cur = n;
  XG->moves[XG->n++] = m;
  XG->keys[XG->n] = n.key;
  xlast_from = XFROM(m); xlast_to = XTO(m);
  offer = -1;                                           /* moving declines a draw offer */
  xsel = xhint_from = xhint_to = -1;
  status[0] = 0;
  xq_refresh_legal();
  int st = xq_game_state(&XG->cur, XG->keys, XG->n + 1);
  if (st) { XG->over = st; clk_live = 0; beep(st <= 2 ? 440 : 660, 250); }
  clock_update_live();
  beep((XFLAGS(m) & XF_CAP) ? 1400 : 1000, 18);
}

static void xq_undo(void) {
  if (!XG->n) return;
  int plies = 1;
  if (cfg.mode == MODE_HC && XG->n >= 2 && !is_human(XG->cur.side ^ 1)) plies = 2;
  int keep = XG->n - plies;
  if (keep < 0) keep = 0;
  XG->cur = XG->start;                                  /* replay from the start (no position history kept) */
  for (int i = 0; i < keep; i++) { XPos n; xq_make_move(&XG->cur, &n, XG->moves[i]); XG->cur = n; }
  XG->n = keep;
  XG->over = 0;
  xsel = xhint_from = xhint_to = xlast_from = xlast_to = -1;
  if (keep) { xlast_from = XFROM(XG->moves[keep - 1]); xlast_to = XTO(XG->moves[keep - 1]); }
  status[0] = 0;
  xq_refresh_legal();
  turn_start = now_ms();
  clock_update_live();
  if (cfg.mode == MODE_HC && !is_human(XG->cur.side)) cfg.human = (int8_t)XG->cur.side;
}

static void xq_info_cb(int depth, int score, uint32_t nodes, Move best) {
  char w[10];
  xq_move_to_wxf(&XG->cur, best, w);
  if (IS_MATE(score)) sprintf(status, "d%d %s M%d", depth, w, (MATE - (score < 0 ? -score : score) + 1) / 2);
  else sprintf(status, "d%d %s %+d", depth, w, score);
  if (show_think) {                                     /* the line, the move on the board, the counts (as chess) */
    think_nodes = nodes;
    think_n = xq_search_pv(&XG->cur, best, think_pv, 12);
    xhint_from = XFROM(best); xhint_to = XTO(best);
    xq_redraw();
    return;
  }
  xq_draw_panel();
  lcd_show();
}

static Result xq_run_search(int depth, uint32_t ms, int noise) {
  Limits lim;
  lim.depth = depth; lim.time_ms = ms; lim.noise = noise; lim.seed = rnd(); lim.max_nodes = 0;
  lim.now = now_ms; lim.poll = poll_cb; lim.info = xq_info_cb;
  thinking = 1;
  think_n = 0; think_nodes = 0;
  last_draw = now_ms();
  strcpy(status, "Thinking...");
  xq_redraw();
  uint32_t t0 = think_t0 = now_ms();
  Result r = xq_search(&XG->cur, XG->keys, XG->n + 1, &lim);
  think_n = 0;
  last_think_ms = now_ms() - t0;
  last_nps = last_think_ms ? (uint32_t)((uint64_t)r.nodes * 1000 / last_think_ms) : r.nodes;
  last_res = r;
  thinking = 0;
  return r;
}

/* the opening book (levels 4+, from the usual start): the classical openings, in Fairy-Stockfish's coordinates
   (ranks 1-10); a random line among those that fit the game so far gives variety */
static const char *const XQ_BOOK[] = {
  "h3e3 h10g8 h1g3 i10h10 i1h1 b10c8 g4g5 g7g6 b1c3 a10b10",   /* 中炮對屏風馬 central cannon, screen horses */
  "h3e3 h10g8 h1g3 b10c8 i1h1 i10h10 g4g5 g7g6 b1c3",
  "h3e3 h10g8 h1g3 i10h10 i1h1 b10c8 b3c3 a10b10 b1a3",       /* 五七炮 */
  "h3e3 h8e8 h1g3 h10g8 i1h1 i10h10 b1c3",                     /* 順炮 same-direction cannons */
  "h3e3 b8e8 h1g3 b10c8 i1h1 a10b10 b1c3",                     /* 列炮 opposite cannons */
  "h3e3 b10c8 h1g3 h8f8 i1h1 h10g8",                           /* 反宮馬 */
  "g1e3 g7g6 h1g3 h10g8 i1h1 i10h10",                          /* 飛相局 elephant opening */
  "g1e3 h8e8 h1g3 h10g8",
  "c1e3 b8e8 b1c3 b10c8",
  "c4c5 g7g6 b1c3 h10g8 h1g3 b10c8",                           /* 仙人指路 pawn opening */
  "c4c5 h8c8 h3e3 c10e8 h1g3",                                 /* 卒底炮 */
  "h1g3 g7g6 c4c5 h10g8 b1c3",                                 /* 起馬局 horse opening */
  "h3d3 h10g8 h1g3 i10h10",                                    /* 過宮炮 */
};

static Move xq_book_move(void) {
  XPos s0;
  xq_pos_start(&s0);
  if (xseven || XG->start.key != s0.key || XG->start.side != XRED || XG->n >= 10) return 0;
  char line[128], *o = line;
  for (int i = 0; i < XG->n; i++) { xq_move_to_str(XG->moves[i], o, 0); o += strlen(o); *o++ = ' '; }
  *o = 0;
  size_t len = strlen(line);
  char cand[16][8];
  int nc = 0;
  for (unsigned i = 0; i < sizeof XQ_BOOK / sizeof *XQ_BOOK && nc < 16; i++) {
    const char *b = XQ_BOOK[i];
    if (strncmp(b, line, len) || strlen(b) <= len) continue;
    int k = 0;
    while (b[len + k] && b[len + k] != ' ' && k < 7) { cand[nc][k] = b[len + k]; k++; }
    cand[nc++][k] = 0;
  }
  if (!nc) return 0;
  const char *pick = cand[rnd() % (uint32_t)nc];
  char t[8];
  for (int i = 0; i < XG->nlegal; i++) { xq_move_to_str(XG->legal[i], t, 0); if (!strcmp(t, pick)) return XG->legal[i]; }
  return 0;
}

static int xq_cpu_turn(void) { return !XG->over && !is_human(XG->cur.side) && XG->nlegal; }

static void xq_cpu_move(void) {
  Move m;
  const Level *L = &LEVELS[cfg.level];
  if (cfg.level >= 4 && (m = xq_book_move()) != 0) {
    sys_delay(200);
    xq_do_move(m);
    sprintf(status, "Book: %s", XG->wxf[XG->n - 1]);
    return;
  }
  if (cfg.level == 0) { m = XG->legal[rnd() % (uint32_t)XG->nlegal]; sys_delay(250); }
  else {
    Result r = xq_run_search(L->depth, think_time(L->ms), L->noise);
    m = r.best ? r.best : XG->legal[0];
    xcpu_score = r.score;
  }
  xq_do_move(m);
}

static void xq_hint(void) {
  if (XG->over || !XG->nlegal) return;
  Result r = xq_run_search(40, 1500, 0);
  char w[10];
  xq_move_to_wxf(&XG->cur, r.best, w);
  xhint_from = XFROM(r.best); xhint_to = XTO(r.best);
  sprintf(status, "Hint: %s", w);
}

/* ------------------------------------------------------------------ */
/*  Keys                                                              */
/* ------------------------------------------------------------------ */
static void xq_move_cursor(int dx, int dy) {
  if (cfg.flip) { dx = -dx; dy = -dy; }
  int f = (XFILE(xcur) + dx + 9) % 9, r = (XRANK(xcur) + dy + 10) % 10;
  xcur = XSQ(f, r);
}

static void xq_select(void) {
  if (XG->over || !is_human(XG->cur.side)) return;
  int pc = XG->cur.b[xcur];
  int mine = pc && XCOLOR(pc) == XG->cur.side;
  int has = 0;
  for (int i = 0; i < XG->nlegal; i++) if (XFROM(XG->legal[i]) == xcur) has = 1;
  if (xsel < 0) { if (mine && has) xsel = xcur; else beep(220, 40); return; }
  if (xcur == xsel) { xsel = -1; return; }
  for (int i = 0; i < XG->nlegal; i++)
    if (XFROM(XG->legal[i]) == xsel && XTO(XG->legal[i]) == xcur) { xq_do_move(XG->legal[i]); return; }
  if (mine && has) xsel = xcur; else beep(220, 40);
}

static void xq_style(void) {
  static const char *const ST[XQ_STYLES] = { "Pieces: characters", "Pieces: symbols", "Pieces: letters" };
  xstyle = (int8_t)((xstyle + 1) % XQ_STYLES);
  strcpy(status, ST[xstyle]);
}

/* the game's end by the players (SHIFT F4 RESIGN, SHIFT F5 DRAW), as chess's local_resign / local_draw */
static void xq_resign(int now) {
  if (XG->over) { strcpy(status, "Game over"); return; }
  if (cfg.mode == MODE_CC) { strcpy(status, "Demo: NEW or MODE"); return; }
  if (!now && confirm != 'R') { confirm = 'R'; strcpy(status, "Resign? RESIGN again"); return; }
  confirm = 0;
  xresigner = (int8_t)(cfg.mode == MODE_HC ? cfg.human : XG->cur.side);
  XG->over = 7; clk_live = 0;
  beep(330, 300);
}

static void xq_draw_offer(void) {
  if (XG->over) { strcpy(status, "Game over"); return; }
  if (cfg.mode == MODE_CC) { strcpy(status, "Demo: no draw"); return; }
  if (cfg.mode == MODE_HH) {
    if (offer == (XG->cur.side ^ 1)) {
      if (confirm != 'A') { confirm = 'A'; strcpy(status, "Draw? ACCEPT again"); return; }
      confirm = 0; offer = -1; XG->over = 8; clk_live = 0; beep(660, 250); return;
    }
    if (offer == XG->cur.side) { strcpy(status, "Offered: now move"); return; }
    offer = (int8_t)XG->cur.side;
    sprintf(status, "%s offers a draw", XG->cur.side ? "Black" : "Red");
    return;
  }
  /* the CPU accepts when it is worse (its last score -30 or less), or from move 20 when it is not better */
  int sc = xcpu_score;
  if (!(sc <= -30 || (sc <= 0 && XG->n >= 40))) { strcpy(status, "CPU declines the draw"); beep(220, 80); return; }
  XG->over = 8; clk_live = 0; beep(660, 250);
}

static void xq_softkey(int k) {
  switch (cfg.page * 6 + (k - KEY_F1)) {
  case 0:  xq_new_game(); break;                                                   /* NEW */
  case 1:  cfg.level = (int8_t)((cfg.level + 1) % 10); sprintf(status, "Level %s", LEVELS[cfg.level].name); break;
  case 2:  cfg.flip ^= 1; break;                                                   /* FLIP */
  case 3:  xq_undo(); break;                                                       /* UNDO */
  case 4:  xq_hint(); break;                                                       /* HINT */
  case 6:  xq_style(); break;                                                      /* PIECES */
  case 7:  clock_key(); break;                                                     /* CLOCK */
  case 8:  cfg.human ^= 1; cfg.flip = cfg.human; if (cfg.mode != MODE_HH) cfg.mode = MODE_HC; xsel = -1;   /* SIDE */
           sprintf(status, "You play %s", cfg.human ? "Black" : "Red"); break;
  case 13: cfg.mode = (int8_t)(cfg.mode == MODE_HC ? MODE_HH : cfg.mode == MODE_HH ? MODE_CC : MODE_HC);  /* MODE */
           xsel = -1; strcpy(status, MODE_NAME[cfg.mode]); break;
  case 12: game_switch(); break;                                                   /* CHESS */
  case 16: show_info(); break;                                                     /* INFO */
  case 5: case 11: case 17: cfg.page = (int8_t)((cfg.page + 1) % 3); break;        /* >> */
  default: strcpy(status, "Chess only"); break;
  }
}

static void xq_handle_key(int key) {
  status[0] = 0;
  if (key >= KEY_F1 && key <= KEY_F6) { xq_softkey(key); return; }
  switch (key) {
  case KEY_UP:   case KEY_8: xq_move_cursor(0, 1); break;
  case KEY_DOWN: case KEY_2: xq_move_cursor(0, -1); break;
  case KEY_4: xq_move_cursor(-1, 0); break;
  case KEY_6: xq_move_cursor(1, 0); break;
  case KEY_7: xq_move_cursor(-1, 1); break;
  case KEY_9: xq_move_cursor(1, 1); break;
  case KEY_1: xq_move_cursor(-1, -1); break;
  case KEY_3: xq_move_cursor(1, -1); break;
  case KEY_5: case KEY_ENTER: xq_select(); break;
  case KEY_EXIT: xsel = xhint_from = xhint_to = -1; break;
  case KEY_BSP: xq_undo(); break;
  case KEY_MUL: xq_style(); break;                                        /* x: the pieces' style */
  case KEY_ADD: case KEY_SUB: zoom ^= 1; strcpy(status, zoom ? "Zoom: +/- back" : "Normal view"); break;
  default: break;
  }
}
