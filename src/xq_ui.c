/*
 * CHESS47 for DM42n (DMCP5) - the Xiangqi game: its screen, keys and moves.
 * (C) 2026 Victor Valdes - GPL-3.0-or-later
 *
 * Included by main.c (it shares the clock, the levels, the modes, the sound and the main loop): SHIFT F1 switches
 * between chess and Xiangqi. The board fills the height on the right, the points 24 px apart; the round pieces of
 * src/xq_pieces.h (the traditional characters: Red 帥仕相傌俥炮兵, Black 將士象馬車砲卒). The panel on the left: the
 * clock, the level, the state, the moves in WXF notation. The soft keys are not drawn (no room) but work as in chess:
 * F1 NEW, F2 LEVEL, F3 FLIP, F4 UNDO, F5 HINT, F6 >> (page 2: F2 CLOCK, F3 SIDE; page 3: F2 MODE); x the pieces:
 * the characters, symbols (crown, shield, elephant, horse, chariot, cannon, soldier) or the WXF letters.
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
static int xcur, xsel = -1, xlast_from = -1, xlast_to = -1, xhint_from = -1, xhint_to = -1;

#define XGAP   24                            /* between two points */
#define XB0    196                           /* file a's x (the board fills x 185 .. 399) */
#define XY0    11                            /* rank 9's y */
#define XPANEL 182

static int xpx(int f) { return XB0 + (cfg.flip ? 8 - f : f) * XGAP; }
static int xpy(int r) { return XY0 + (cfg.flip ? r : 9 - r) * XGAP; }

static int xq_side(void) { return XG->cur.side; }
static int xq_over_n(int *n) { *n = XG->n; return XG->over; }

static void xq_refresh_legal(void) { XG->nlegal = xq_gen_legal(&XG->cur, XG->legal); }

static void xq_new_game(void) {
  xq_pos_start(&XG->start);
  XG->cur = XG->start;
  XG->keys[0] = XG->cur.key;
  XG->n = XG->over = 0;
  xsel = xhint_from = xhint_to = xlast_from = xlast_to = -1;
  xcur = XSQ(7, 2);                                     /* Red's cannon: C2=5 is the most common first move */
  status[0] = 0;
  clock_reset();
  eng_clear_tt();
  xq_refresh_legal();
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
  int ym = (xpy(4) + xpy(5)) / 2 - XQ_RSIZE / 2;
  for (int k = 0; k < 4; k++) {
    int x0 = (k < 2 ? XB0 + 26 : XB0 + 5 * XGAP + 10) + (k & 1) * 22;
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
  const uint8_t *bm = XQ_PIECE[xstyle][XCOLOR(pc)][XTYPE(pc) - 1];
  int x0 = cx - XQ_PSIZE / 2, y0 = cy - XQ_PSIZE / 2;
  for (int y = 0; y < XQ_PSIZE; y++) {
    uint32_t ink = (uint32_t)bm[3 * y] << 16 | bm[3 * y + 1] << 8 | bm[3 * y + 2];
    uint32_t dsc = (uint32_t)XQ_DISC[3 * y] << 16 | XQ_DISC[3 * y + 1] << 8 | XQ_DISC[3 * y + 2];
    for (int x = 0; x < XQ_PSIZE; ) {                   /* runs of one value */
      if (!(dsc >> (23 - x) & 1)) { x++; continue; }
      int v = (int)(ink >> (23 - x) & 1) ^ inv, x1 = x;
      while (x1 < XQ_PSIZE && (dsc >> (23 - x1) & 1) && ((int)(ink >> (23 - x1) & 1) ^ inv) == v) x1++;
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

static void xq_corners(int cx, int cy, int r) {         /* the last move, a hint: four corners */
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2) {
      lcd_fill_rect(sx < 0 ? cx - r : cx + r - 3, cy + sy * r, 4, 1, 1);
      lcd_fill_rect(cx + sx * r, sy < 0 ? cy - r : cy + r - 3, 1, 4, 1);
    }
}

static void xq_draw_board(void) {
  lcd_fill_rect(XPANEL + 2, 0, LCD_X - XPANEL - 2, LCD_Y, 0);
  xq_grid();
  for (int r = 0; r < 10; r++)
    for (int f = 0; f < 9; f++) {
      int s = XSQ(f, r), pc = XG->cur.b[s], cx = xpx(f), cy = xpy(r);
      if (pc) xq_piece(pc, cx, cy, s == xsel);
      if (xq_is_target(s)) {
        if (pc) xq_frame(cx, cy, 12, 1);
        else lcd_fill_rect(cx - 2, cy - 2, 5, 5, 1);
      }
      if (s == xlast_from || s == xlast_to || s == xhint_from || s == xhint_to) xq_corners(cx, cy, 12);
      if (s == xcur && !thinking) xq_frame(cx, cy, 13, 2);
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
  case 7: return loser == XRED ? "Red resigns 0-1" : "Black resigns 1-0";
  }
  return "";
}

static void xq_draw_clock(void) {
  int h = lh(t20) + 2;
  lcd_fill_rect(0, 0, XPANEL + 1, h, 0);
  if (!cfg.tc) { xptext(2, 1, "XIANGQI 47", 1); return; }
  char a[16], b[16], s[24];
  fmt_clock(clock_left(XRED), a);
  fmt_clock(clock_left(XBLACK), b);
  sprintf(s, " R %s ", a);
  xptext(0, 1, s, XG->cur.side == XRED && !XG->over);
  sprintf(s, " B %s ", b);
  xptext(XPANEL / 2 + 2, 1, s, XG->cur.side == XBLACK && !XG->over);
  lcd_fill_rect(XPANEL / 2, 0, 1, h, 1);
}

static void xq_draw_panel(void) {
  char s[48];
  int h = lh(t20), y;
  lcd_fill_rect(0, 0, XPANEL + 1, LCD_Y, 0);
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
  else if (status[0]) snprintf(s, sizeof s, "%s", status);
  else sprintf(s, "%s to move", XG->cur.side ? "Black" : "Red");
  xptext(2, y, s, inv);
  y += h + 2;
  lcd_fill_rect(2, y, XPANEL - 4, 1, 1);
  y += 3;
  /* the moves, two a line (WXF), the last that fit */
  int bottom = LCD_Y - h - 2, lines = (bottom - y) / h;
  int total = (XG->n + 1) / 2, first = total - lines;
  if (first < 0) first = 0;
  for (int ln = first; ln < total; ln++) {
    sprintf(s, "%d.", ln + 1);
    xptext(2, y, s, 0);
    xptext(36, y, XG->wxf[2 * ln], 0);
    if (2 * ln + 1 < XG->n) xptext(108, y, XG->wxf[2 * ln + 1], 0);
    y += h;
  }
  static const char *const PAGE[3] = { "F1 NEW F2 LVL F4 UNDO", "F2 CLOCK F3 SIDE", "F2 MODE F5 INFO" };
  xptext(2, LCD_Y - h, PAGE[cfg.page], 0);
}

static void xq_redraw(void) {
  lcd_clear_buf();
  xq_draw_board();
  xq_draw_panel();
  lcd_show();
  if (cfg.usb) usbview_send();
}

/* ------------------------------------------------------------------ */
/*  Moves, the computer                                               */
/* ------------------------------------------------------------------ */
static void xq_do_move(Move m) {
  if (XG->n >= MAXGAME) { strcpy(status, "Game too long"); return; }
  int mover = XG->cur.side;
  if (clk_live) {
    clk[mover] -= (int32_t)(now_ms() - turn_start);
    if (clk[mover] <= 0) { clk[mover] = 0; XG->over = 6; clk_live = 0; beep(330, 400); return; }
    clk[mover] += (int32_t)TCS[cfg.tc].inc_s * 1000;
  }
  turn_start = now_ms();
  xq_move_to_wxf(&XG->cur, m, XG->wxf[XG->n]);
  XPos n;
  xq_make_move(&XG->cur, &n, m);
  XG->cur = n;
  XG->moves[XG->n++] = m;
  XG->keys[XG->n] = n.key;
  xlast_from = XFROM(m); xlast_to = XTO(m);
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
  (void)nodes;
  xq_move_to_wxf(&XG->cur, best, w);
  if (IS_MATE(score)) sprintf(status, "d%d %s M%d", depth, w, (MATE - (score < 0 ? -score : score) + 1) / 2);
  else sprintf(status, "d%d %s %+d", depth, w, score);
  xq_draw_panel();
  lcd_show();
}

static Result xq_run_search(int depth, uint32_t ms, int noise) {
  Limits lim;
  lim.depth = depth; lim.time_ms = ms; lim.noise = noise; lim.seed = rnd(); lim.max_nodes = 0;
  lim.now = now_ms; lim.poll = poll_cb; lim.info = xq_info_cb;
  thinking = 1;
  last_draw = now_ms();
  strcpy(status, "Thinking...");
  xq_redraw();
  uint32_t t0 = now_ms();
  Result r = xq_search(&XG->cur, XG->keys, XG->n + 1, &lim);
  last_think_ms = now_ms() - t0;
  last_nps = last_think_ms ? (uint32_t)((uint64_t)r.nodes * 1000 / last_think_ms) : r.nodes;
  last_res = r;
  thinking = 0;
  return r;
}

static int xq_cpu_turn(void) { return !XG->over && !is_human(XG->cur.side) && XG->nlegal; }

static void xq_cpu_move(void) {
  Move m;
  const Level *L = &LEVELS[cfg.level];
  if (cfg.level == 0) { m = XG->legal[rnd() % (uint32_t)XG->nlegal]; sys_delay(250); }
  else {
    Result r = xq_run_search(L->depth, think_time(L->ms), L->noise);
    m = r.best ? r.best : XG->legal[0];
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

static void xq_softkey(int k) {
  switch (cfg.page * 6 + (k - KEY_F1)) {
  case 0:  xq_new_game(); break;                                                   /* NEW */
  case 1:  cfg.level = (int8_t)((cfg.level + 1) % 10); sprintf(status, "Level %s", LEVELS[cfg.level].name); break;
  case 2:  cfg.flip ^= 1; break;                                                   /* FLIP */
  case 3:  xq_undo(); break;                                                       /* UNDO */
  case 4:  xq_hint(); break;                                                       /* HINT */
  case 7:  cfg.tc = (int8_t)((cfg.tc + 1) % TC_CNT); clock_reset(); clock_update_live();   /* CLOCK */
           sprintf(status, "Clock %s", TCS[cfg.tc].name); break;
  case 8:  cfg.human ^= 1; cfg.flip = cfg.human; if (cfg.mode != MODE_HH) cfg.mode = MODE_HC; xsel = -1;   /* SIDE */
           sprintf(status, "You play %s", cfg.human ? "Black" : "Red"); break;
  case 13: cfg.mode = (int8_t)(cfg.mode == MODE_HC ? MODE_HH : cfg.mode == MODE_HH ? MODE_CC : MODE_HC);  /* MODE */
           xsel = -1; strcpy(status, MODE_NAME[cfg.mode]); break;
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
  case KEY_MUL: {                                                         /* x: the pieces' style */
    static const char *const ST[XQ_STYLES] = { "Pieces: characters", "Pieces: symbols", "Pieces: letters" };
    xstyle = (int8_t)((xstyle + 1) % XQ_STYLES);
    strcpy(status, ST[xstyle]);
    break;
  }
  default: break;
  }
}
