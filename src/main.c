/*
 * CHESS 47 for SwissMicros DM42n (DMCP5 native program, .pg5)
 * (C) 2026 Victor Valdes (valdes.vj@gmail.com) - MIT License
 *
 * Built with the SwissMicros DMCP5 SDK (BSD-3, NOMAS).
 *
 * Screen (400x240):  left panel 0..191 = clock, level, status, moves
 *                    right 192..399    = 8x8 board, 26 px squares
 *                    bottom 32 lines   = soft-key menu (F1..F6)
 */
#include <main.h>
#include <dmcp.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "usbview.h"
#include "usbser.h"
#include "pieces.h"

/* ------------------------------------------------------------------ */
/*  Layout                                                            */
/* ------------------------------------------------------------------ */
#define SQPX     26
#define BX       192
#define BY       0
#define BOARD_PX (8 * SQPX)
#define PANEL_W  186
#define DIR      "/CHESS47"

/* ------------------------------------------------------------------ */
/*  Game state - on the heap (DMCP5 programs have 8 KB static RAM)    */
/* ------------------------------------------------------------------ */
typedef struct {
  Pos      hist[MAXGAME + 1];
  uint64_t keys[MAXGAME + 1];
  Move     moves[MAXGAME];
  char     san[MAXGAME][8];
  int      n;                 /* plies played */
  int      pos960;            /* -1 = classic */
  int      over;              /* 0 playing, else reason code (see result_text) */
  Move     legal[MAXMOVES];
  int      nlegal;
  char     fbuf[4096];        /* file I/O and PGN text */
} Game;
static Game *G;

#define CUR (&G->hist[G->n])

enum { MODE_HC = 0, MODE_HH, MODE_CC, MODE_USB, MODE_EUSB, MODE_CNT };
static const char *MODE_NAME[MODE_CNT] = { "You vs CPU", "2 players", "CPU vs CPU", "You vs USB", "CPU vs USB" };
#define VS_USB (cfg.mode == MODE_USB || cfg.mode == MODE_EUSB)

typedef struct {
  int8_t level;      /* 0 = CRAZY, 1..9 */
  int8_t mode;
  int8_t human;      /* colour played by the human in MODE_HC and MODE_USB; by the calculator's engine in MODE_EUSB */
  int8_t flip;
  int8_t tc;         /* time control index */
  int8_t sound;
  int8_t usb;
  int8_t page;
} Cfg;
static Cfg cfg = { 4, MODE_HC, WHITE, 0, 0, 1, 0, 0 };

typedef struct { const char *name; int depth; uint32_t ms; int noise; } Level;
static const Level LEVELS[10] = {
  { "CRAZY", 0, 0, 0 },
  { "1",  1,     0, 150 }, { "2",  2,     0, 80 }, { "3",  3,     0, 40 },
  { "4",  5,   500,   6 }, { "5", 40,  1000,  2 }, { "6", 40,  2500,  0 },
  { "7", 40,  5000,   0 }, { "8", 40, 10000,  0 }, { "9", 40, 30000,  0 },
};

typedef struct { const char *name; uint32_t base_s, inc_s; } TimeCtl;
static const TimeCtl TCS[] = {
  { "OFF", 0, 0 }, { "1+0", 60, 0 }, { "3+2", 180, 2 }, { "5+0", 300, 0 },
  { "10+0", 600, 0 }, { "15+10", 900, 10 }, { "30+0", 1800, 0 }, { "60+0", 3600, 0 },
};
#define TC_CNT ((int)(sizeof TCS / sizeof *TCS))

/* clock */
static int32_t  clk[2];
static uint32_t turn_start;
static int      clk_live;   /* clock currently ticking */

/* UI */
static int  cur = 0x14, sel = NOSQ;
static int  last_from = NOSQ, last_to = NOSQ, hint_from = NOSQ, hint_to = NOSQ;
static int  shift;
static int  promo_mode;            /* waiting for promotion choice */
static Move promo_opts[4];
static int  num_mode, num_len;     /* 960 number entry */
static char num_buf[4];
static char status[40];
static int  thinking;
static uint32_t last_nps, last_think_ms;
static Result   last_res;
static uint32_t last_tick;
static uint32_t rng = 0x47474747u;
/* CPU vs USB: the PC holds the engine until "go" (while it catches the board up) and gives the game's clock */
static int      usb_hold;
static char     end_txt[40];               /* "end <text>": how the game ended on the PC's side */
static int32_t  usb_left = -1, usb_inc;    /* the engine's time left and increment, ms (-1: no clock) */

/* ------------------------------------------------------------------ */
/*  Small helpers                                                     */
/* ------------------------------------------------------------------ */
static uint32_t now_ms(void) { return sys_current_ms(); }
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void beep(int hz, int ms) {
  if (!cfg.sound || ST(STAT_BEEP_MUTE)) return;
  start_buzzer_freq((uint32_t)hz * 1000);
  sys_delay(ms);
  stop_buzzer();
}

static int is_human(int side) {
  if (cfg.mode == MODE_HH) return 1;
  if (cfg.mode == MODE_CC || cfg.mode == MODE_EUSB) return 0;
  return side == cfg.human;
}

/* the side whose moves come over USB (vs USB modes) */
static int usb_side(int side) { return VS_USB && side != cfg.human; }

static void fmt_clock(int32_t ms, char *s) {
  if (ms < 0) ms = 0;
  int t = (ms + 999) / 1000;
  if (t >= 3600) sprintf(s, "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
  else if (ms < 10000) sprintf(s, "%d.%d", (int)(ms / 1000), (int)((ms / 100) % 10));
  else sprintf(s, "%d:%02d", t / 60, t % 60);
}

static int32_t clock_left(int side) {
  int32_t c = clk[side];
  if (clk_live && CUR->side == side) c -= (int32_t)(now_ms() - turn_start);
  return c;
}

static void sq_name(int sq, char *s) { s[0] = (char)('a' + FILE_OF(sq)); s[1] = (char)('1' + RANK_OF(sq)); s[2] = 0; }

/* ------------------------------------------------------------------ */
/*  Graphics                                                          */
/* ------------------------------------------------------------------ */
/* Row renderer: build one pixel row then emit runs with lcd_fill_rect */
static uint8_t rowbuf[BOARD_PX];

static void emit_row(int x0, int y, const uint8_t *px, int w) {
  int i = 0;
  while (i < w) {
    int j = i;
    while (j < w && px[j] == px[i]) j++;
    lcd_fill_rect(x0 + i, y, j - i, 1, px[i]);
    i = j;
  }
}

static int bmp_at(const char * const *bmp, int w, int h, int x, int y) {
  if (x < 0 || y < 0 || x >= w || y >= h) return 0;
  char c = bmp[y][x];
  return c == '#' ? 1 : c == '+' ? 2 : 0;
}

/* pixel value of a piece glyph at (x,y) in glyph space: -1 transparent, 0 white, 1 black */
static int piece_px(int pc, int x, int y) {
  const char * const *bmp = PIECE_BMP[PTYPE(pc)];
  int v = bmp_at(bmp, PIECE_W, PIECE_H, x, y);
  if (!v) {
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (bmp_at(bmp, PIECE_W, PIECE_H, x + dx, y + dy)) return 0;  /* halo */
    return -1;
  }
  if (PCOLOR(pc) == BLACK) return v == 2 ? 0 : 1;
  if (v == 2) return 1;
  /* white piece: outline where a 4-neighbour is outside the body */
  if (!bmp_at(bmp, PIECE_W, PIECE_H, x - 1, y) || !bmp_at(bmp, PIECE_W, PIECE_H, x + 1, y) ||
      !bmp_at(bmp, PIECE_W, PIECE_H, x, y - 1) || !bmp_at(bmp, PIECE_W, PIECE_H, x, y + 1)) return 1;
  return 0;
}

static int sq_x(int sq) { int f = FILE_OF(sq); return BX + (cfg.flip ? 7 - f : f) * SQPX; }
static int sq_y(int sq) { int r = RANK_OF(sq); return BY + (cfg.flip ? r : 7 - r) * SQPX; }

static int is_target(int sq) {
  if (sel == NOSQ) return 0;
  for (int i = 0; i < G->nlegal; i++) {
    Move m = G->legal[i];
    if (MFROM(m) != sel) continue;
    if (MTO(m) == sq && !(MFLAGS(m) & F_CASTLE)) return 1;
    if ((MFLAGS(m) & F_CASTLE) && castle_rook_sq(CUR, m) == sq) return 1;
  }
  return 0;
}

static void draw_square(int sq) {
  const Pos *p = CUR;
  int x0 = sq_x(sq), y0 = sq_y(sq);
  int dark = !((FILE_OF(sq) + RANK_OF(sq)) & 1);
  int pc = p->b[sq];
  int target = is_target(sq);
  int ox = (SQPX - PIECE_W) / 2, oy = (SQPX - PIECE_H) / 2 + 1;
  for (int y = 0; y < SQPX; y++) {
    for (int x = 0; x < SQPX; x++) {
      int v = dark ? (((x + y) & 1) == 0 && (y & 1) == 0) : 0;
      if (pc) { int q = piece_px(pc, x - ox, y - oy); if (q >= 0) v = q; }
      /* destination marker: small dot on empty squares, corners on captures */
      if (target) {
        if (!pc && x >= 11 && x <= 14 && y >= 11 && y <= 14) v = 1;
        if (pc && ((x < 4 && y < 4 && x + y < 4) || (x > 21 && y < 4 && (25 - x) + y < 4) ||
                   (x < 4 && y > 21 && x + (25 - y) < 4) || (x > 21 && y > 21 && (25 - x) + (25 - y) < 4))) v = 1;
      }
      /* last move: thin frame */
      if ((sq == last_from || sq == last_to) && (x == 0 || y == 0 || x == SQPX - 1 || y == SQPX - 1)) v = 1;
      /* hint: dashed frame */
      if ((sq == hint_from || sq == hint_to) && (x <= 1 || y <= 1 || x >= SQPX - 2 || y >= SQPX - 2) && ((x + y) & 2)) v = 1;
      rowbuf[x] = (uint8_t)v;
    }
    /* selection: invert the whole square border 3px; cursor: 2px solid frame */
    if (sq == sel) {
      for (int x = 0; x < SQPX; x++)
        if (x < 3 || x >= SQPX - 3 || y < 3 || y >= SQPX - 3) rowbuf[x] = 1;
    }
    if (sq == cur && !thinking) {
      for (int x = 0; x < SQPX; x++) {
        int edge = x < 2 || x >= SQPX - 2 || y < 2 || y >= SQPX - 2;
        int inner = (x == 2 || x == SQPX - 3 || y == 2 || y == SQPX - 3) && !(x < 2 || x >= SQPX - 2 || y < 2 || y >= SQPX - 2);
        if (edge) rowbuf[x] = 1;
        else if (inner) rowbuf[x] = 0;
      }
    }
    emit_row(x0, y0 + y, rowbuf, SQPX);
  }
}

static void draw_board(void) {
  for (int r = 0; r < 8; r++)
    for (int f = 0; f < 8; f++) draw_square(SQ(f, r));
  /* frame */
  lcd_fill_rect(BX - 1, BY, 1, BOARD_PX, 1);
}

static void draw_bitmap(const char * const *bmp, int w, int h, int x0, int y0, int z) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (bmp[y][x] == '#') lcd_fill_rect(x0 + x * z, y0 + y * z, z, z, 1);
}

/* text at pixel position, never touching the rest of the line */
static void text_at(disp_stat_t *ds, int x, int y, const char *s, int inv) {
  uint8_t lnf = ds->lnfill, nl = ds->newln;
  ds->lnfill = 0; ds->newln = 0; ds->inv = (uint8_t)inv;
  lcd_setXY(ds, x, y);
  lcd_writeText(ds, s);
  ds->inv = 0; ds->lnfill = lnf; ds->newln = nl;
}

static int lh(disp_stat_t *ds) { return lcd_lineHeight(ds); }

static void draw_clock(void) {
  int h = lh(t20) + 2;
  lcd_fill_rect(0, 0, PANEL_W + 4, h, 0);
  if (!cfg.tc) {
    text_at(t20, 2, 1, "CHESS 47", 1);
    char s[16];
    sprintf(s, "%s", chess960 ? (G->pos960 >= 0 ? "960" : "FEN") : "");
    if (G->pos960 >= 0) sprintf(s, "960 #%d", G->pos960);
    text_at(t20, PANEL_W - lcd_textWidth(t20, s), 1, s, 0);
    return;
  }
  char a[16], b[16];
  fmt_clock(clock_left(WHITE), a);
  fmt_clock(clock_left(BLACK), b);
  int stm = CUR->side;
  char s[24];
  sprintf(s, " W %s ", a);
  text_at(t20, 0, 1, s, stm == WHITE && !G->over);
  sprintf(s, " B %s ", b);
  text_at(t20, PANEL_W / 2 + 2, 1, s, stm == BLACK && !G->over);
  lcd_fill_rect(PANEL_W / 2, 0, 1, h, 1);
}

static const char *result_text(int code, int loser) {
  switch (code) {
  case 1: return loser == WHITE ? "Checkmate  0-1" : "Checkmate  1-0";
  case 2: return "Stalemate  1/2";
  case 3: return "50 moves  1/2";
  case 4: return "No material 1/2";
  case 5: return "Repetition 1/2";
  case 6: return loser == WHITE ? "W flag fell 0-1" : "B flag fell 1-0";
  case 7: return loser == WHITE ? "W resigns 0-1" : "B resigns 1-0";
  case 8: return end_txt[0] ? end_txt : "Game over";
  }
  return "";
}

static const char *result_tag(void) {
  int loser = CUR->side;
  switch (G->over) {
  case 1: case 6: case 7: return loser == WHITE ? "0-1" : "1-0";
  case 2: case 3: case 4: case 5: return "1/2-1/2";
  }
  return "*";
}

static int material(void) {
  static const int v[7] = { 0, 1, 3, 3, 5, 9, 0 };
  int s = 0;
  for (int q = 0; q < 128; q++)
    if (ONBOARD(q) && CUR->b[q]) s += PCOLOR(CUR->b[q]) ? -v[PTYPE(CUR->b[q])] : v[PTYPE(CUR->b[q])];
  return s;
}

static void draw_panel(void) {
  char s[48];
  int h = lh(t20);
  lcd_fill_rect(0, 0, BX - 1, BOARD_PX, 0);
  draw_clock();
  int y = h + 4;

  /* level + mode */
  char lv[8];
  sprintf(lv, cfg.level ? "L%s" : "%s", LEVELS[cfg.level].name);
  if (cfg.mode == MODE_HH) sprintf(s, "2 players");
  else if (cfg.mode == MODE_CC) sprintf(s, "Demo  %s", lv);
  else if (cfg.mode == MODE_USB) sprintf(s, "vs USB  You:%s", cfg.human ? "Black" : "White");
  else if (cfg.mode == MODE_EUSB) sprintf(s, "%s vs USB  CPU:%s", lv, cfg.human ? "Black" : "White");
  else sprintf(s, "%s  You:%s", lv, cfg.human ? "Black" : "White");
  text_at(t20, 2, y, s, 0);
  y += h;

  /* status line */
  /* the game's state first (the result, check), then the engine's line: the search used to hide "check" */
  int inv = 0;
  if (num_mode) sprintf(s, "960 #: %s_", num_buf);
  else if (promo_mode) sprintf(s, "Promote to?");
  else if (G->over) { strcpy(s, result_text(G->over, CUR->side)); inv = 1; }
  else if (in_check(CUR)) { sprintf(s, "CHECK! %s to move", CUR->side ? "Black" : "White"); inv = 1; }
  else if (status[0]) strcpy(s, status);
  else sprintf(s, "%s to move", CUR->side ? "Black" : "White");
  lcd_fill_rect(0, y, BX - 1, h, 0);
  text_at(t20, 2, y, s, inv);
  y += h + 2;
  lcd_fill_rect(2, y, PANEL_W - 4, 1, 1);
  y += 3;

  /* move list: last lines that fit */
  int bottom = cfg.level == 0 && cfg.mode != MODE_HH ? BOARD_PX - 46 : BOARD_PX - h;
  int lines = (bottom - y) / h;
  if (lines < 1) lines = 1;
  int total = (G->n + 1 + (G->hist[0].side == BLACK)) / 2;
  int first = total - lines;
  if (first < 0) first = 0;
  int base = G->hist[0].fullmove;
  for (int ln = first; ln < total; ln++) {
    int iw = ln * 2 - (G->hist[0].side == BLACK);      /* ply index of white's move */
    char w[10] = "", b[10] = "";
    if (iw >= 0 && iw < G->n) strcpy(w, G->san[iw]);
    else if (iw < 0) strcpy(w, "...");
    if (iw + 1 >= 0 && iw + 1 < G->n) strcpy(b, G->san[iw + 1]);
    sprintf(s, "%d.", base + ln);
    text_at(t20, 2, y, s, 0);
    text_at(t20, 40, y, w, 0);
    text_at(t20, 112, y, b, 0);
    y += h;
  }

  /* footer */
  if (cfg.level == 0 && cfg.mode != MODE_HH) {
    draw_bitmap(CRAZY_BMP, CRAZY_W, CRAZY_H, 4, BOARD_PX - 46, 2);
    text_at(t20, 60, BOARD_PX - 36, "CRAZY!", 1);
  } else {
    int m = material();
    char c[3]; sq_name(cur, c);
    sprintf(s, "%s  %s%d", c, m > 0 ? "+" : "", m);
    if (cfg.usb) strcat(s, "  USB");
    text_at(t20, 2, BOARD_PX - h, s, 0);
  }
}

static const char *MENU[3][6] = {
  { "NEW", "LEVEL", "FLIP", "UNDO", "HINT", ">>" },
  { "960", "CLOCK", "SIDE", "SAVE", "LOAD", ">>" },
  { "PGN", "MODE", "QR", "USB", "INFO", ">>" },
};
static const char *PROMO_MENU[6] = { "Queen", "Rook", "Bishop", "Knight", "", "Cancel" };

/* the LCD is sent by DMA in the background: wait until it is out before the buffer is touched again
   (otherwise the next lcd_clear_buf, a move later, blanks the lower lines still being sent: the flicker of the demo) */
static void lcd_show(void) {
  lcd_refresh();
  lcd_refresh_wait();
}

/* the end of the game, in a box on the board: what happened, why, the result */
static void draw_banner(void) {
  if (!G->over) return;
  const char *L[3] = { "", "", "" };
  int loser = CUR->side;
  const char *win = loser == WHITE ? "Black wins  0-1" : "White wins  1-0";
  switch (G->over) {
  case 1: L[0] = "CHECKMATE"; L[2] = win; break;
  case 2: L[0] = "STALEMATE"; L[1] = "no legal move"; L[2] = "Draw  1/2-1/2"; break;
  case 3: L[0] = "DRAW"; L[1] = "50-move rule"; L[2] = "1/2-1/2"; break;
  case 4: L[0] = "DRAW"; L[1] = "no mating material"; L[2] = "1/2-1/2"; break;
  case 5: L[0] = "DRAW"; L[1] = "3-fold repetition"; L[2] = "1/2-1/2"; break;
  case 6: L[0] = "TIME"; L[1] = loser == WHITE ? "White's flag fell" : "Black's flag fell"; L[2] = win; break;
  case 7: L[0] = "RESIGNS"; L[1] = loser == WHITE ? "White resigns" : "Black resigns"; L[2] = win; break;
  case 8: L[0] = "GAME OVER"; L[1] = end_txt; break;
  default: return;
  }
  int h24 = lh(t24), h20 = lh(t20), n = L[1][0] ? 3 : 2;
  int bh = h24 + (n - 1) * h20 + 12, bw = BOARD_PX - 8;
  int x0 = BX + 4, y0 = BY + (BOARD_PX - bh) / 2;
  lcd_fill_rect((uint32_t)x0 - 2, (uint32_t)y0 - 2, (uint32_t)bw + 4, (uint32_t)bh + 4, 0);   /* a white rim */
  lcd_fill_rect((uint32_t)x0, (uint32_t)y0, (uint32_t)bw, (uint32_t)bh, 1);
  int y = y0 + 5;
  text_at(t24, x0 + (bw - lcd_textWidth(t24, L[0])) / 2, y, L[0], 1);
  y += h24;
  for (int i = 1; i < 3; i++) {
    if (!L[i][0]) continue;
    text_at(t20, x0 + (bw - lcd_textWidth(t20, L[i])) / 2, y, L[i], 1);
    y += h20;
  }
}

static void draw_menu(void) {
  const char *keys[6];
  for (int i = 0; i < 6; i++) keys[i] = MENU[cfg.page][i];

  if (cfg.page == 1 && chess960) keys[0] = "960*";

  lcd_draw_menu_keys(keys);
}

static void redraw(void) {
  lcd_clear_buf();
  draw_board();
  draw_banner();
  draw_panel();
  draw_menu();
  lcd_show();
  if (cfg.usb) usbview_send();                 /* the screen to the PC (tools/dm42view.py) */
}

/* ------------------------------------------------------------------ */
/*  Game logic                                                        */
/* ------------------------------------------------------------------ */
static void refresh_legal(void) { G->nlegal = gen_legal(CUR, G->legal); }

static void usb_send(const char *s) {
  if (!cfg.usb) return;
  usbser_puts(s);
}

/* vs USB: tell the PC which side it plays (tools/usb_stockfish.py, a Lichess bridge ...) */
static void usb_opponent(void) {
  if (!VS_USB) return;
  usb_send(cfg.human ? "opponent white\r\n" : "opponent black\r\n");
}

static void clock_reset(void) {
  clk[0] = clk[1] = (int32_t)TCS[cfg.tc].base_s * 1000;
  clk_live = 0;
}

static void clock_update_live(void) {
  int want = cfg.tc && !G->over && G->n > 0;
  if (want && !clk_live) turn_start = now_ms();
  clk_live = want;
}

static void new_game_from(const Pos *start, int pos960) {
  G->hist[0] = *start;
  G->keys[0] = start->key;
  G->n = 0;
  G->over = 0;
  G->pos960 = pos960;
  sel = hint_from = hint_to = last_from = last_to = NOSQ;
  cur = cfg.flip ? 0x64 : 0x14;
  status[0] = 0;
  promo_mode = 0;
  clock_reset();
  eng_clear_tt();
  refresh_legal();
  if (cfg.usb) {
    char fen[100], s[120];
    pos_to_fen(start, fen);
    sprintf(s, "\r\nnewgame %s\r\n", fen);
    usb_send(s);
    usb_opponent();
  }
}

static void new_game(int pos960) {
  Pos p;
  if (pos960 < 0) pos_start(&p);
  else pos_960(&p, pos960);
  new_game_from(&p, pos960);
}

static void check_over(void) {
  int st = game_state(CUR, G->keys, G->n + 1);
  if (st) {
    G->over = st;
    clk_live = 0;
    beep(st == 1 ? 440 : 660, 250);
    if (cfg.usb) {
      char s[48];
      sprintf(s, "result %s %s\r\n", result_tag(), result_text(st, CUR->side));
      usb_send(s);
    }
  }
}

static void do_move(Move m) {
  if (G->n >= MAXGAME) { strcpy(status, "Game too long"); return; }
  Pos *p = CUR;
  int mover = p->side;
  /* clock */
  if (clk_live) {
    clk[mover] -= (int32_t)(now_ms() - turn_start);
    if (clk[mover] <= 0) { clk[mover] = 0; G->over = 6; clk_live = 0; beep(330, 400); return; }
    clk[mover] += (int32_t)TCS[cfg.tc].inc_s * 1000;
  }
  turn_start = now_ms();
  move_to_san(p, m, G->san[G->n]);
  if (cfg.usb) {
    char s[16];
    move_to_str(p, m, s);
    strcat(s, "\r\n");
    usb_send(s);
  }
  make_move(p, &G->hist[G->n + 1], m);
  G->moves[G->n] = m;
  G->n++;
  G->keys[G->n] = CUR->key;
  last_from = MFROM(m);
  last_to = (MFLAGS(m) & F_CASTLE) ? MTO(m) : MTO(m);
  sel = hint_from = hint_to = NOSQ;
  status[0] = 0;
  refresh_legal();
  check_over();
  clock_update_live();
  beep((MFLAGS(m) & F_CAP) ? 1400 : 1000, 18);
}

static void undo(void) {
  if (!G->n) return;
  if (VS_USB) { strcpy(status, "No undo vs USB"); return; }
  int plies = 1;
  if (cfg.mode == MODE_HC && G->n >= 2 && !is_human(G->hist[G->n - 1].side)) plies = 2;
  if (cfg.mode == MODE_HC && G->n == 1 && !is_human(G->hist[0].side)) plies = 1;
  G->n -= plies;
  if (G->n < 0) G->n = 0;
  G->over = 0;
  sel = hint_from = hint_to = NOSQ;
  last_from = last_to = NOSQ;
  if (G->n) { last_from = MFROM(G->moves[G->n - 1]); last_to = MTO(G->moves[G->n - 1]); }
  status[0] = 0;
  refresh_legal();
  turn_start = now_ms();
  clock_update_live();
  /* in "You vs CPU" an undo that lands on the CPU's turn would make it move
     again immediately; switch sides instead so the human keeps control */
  if (cfg.mode == MODE_HC && !is_human(CUR->side)) cfg.human = CUR->side;
}

/* ------------------------------------------------------------------ */
/*  Computer                                                          */
/* ------------------------------------------------------------------ */
static uint32_t last_draw;

static int deferred_key = -1;   /* key pressed while the CPU was thinking */

static int poll_cb(void) {
  int stop = 0;
  while (!key_empty()) {
    int k = key_pop();
    if (k <= 0) continue;                                   /* key release */
    if (k == KEY_EXIT || k == KEY_ENTER || k == KEY_5) { stop = 1; continue; }  /* move now */
    if (deferred_key < 0) deferred_key = k;                 /* handle it after the move */
    if (cfg.mode == MODE_CC) stop = 1;                      /* demo: any key interrupts */
  }
  usbser_task();                                            /* the USB keeps answering the PC during the search */
  uint32_t t = now_ms();
  if (clk_live && clock_left(CUR->side) <= 0) stop = 1;
  if (t - last_draw > 500) {
    last_draw = t;
    draw_clock();
    lcd_refresh_lines(0, lh(t20) + 2); lcd_refresh_wait();
    reset_auto_off();
  }
  return stop;
}

static void info_cb(int depth, int score, uint32_t nodes, Move best) {
  char san[10];
  move_to_san(CUR, best, san);
  if (IS_MATE(score)) {
    int m = (MATE - (score < 0 ? -score : score) + 1) / 2;
    sprintf(status, "d%d %s M%s%d", depth, san, score < 0 ? "-" : "", m);
  } else {
    sprintf(status, "d%d %s %+d.%02d", depth, san, score / 100, (score < 0 ? -score : score) % 100);
  }
  (void)nodes;
#ifdef SIM_DEBUG
  fprintf(stderr, "info d%d %d nodes %lu %s\n", depth, score, (unsigned long)nodes, san);
#endif
  draw_panel();
  lcd_show();
}

static uint32_t think_time(uint32_t level_ms) {
  if (cfg.mode == MODE_EUSB && usb_left >= 0) {          /* the online game's clock, from the PC */
    int32_t left = usb_left - 1500;                      /* the USB and Lichess's lag */
    int32_t t = left / 30 + usb_inc * 3 / 4;
    if (t > left / 4) t = left / 4;
    if (t < 50) t = 50;
    if (level_ms && (uint32_t)t > level_ms) t = (int32_t)level_ms;
    return (uint32_t)t;
  }
  if (!cfg.tc) return level_ms;
  int32_t left = clock_left(CUR->side);
  int32_t t = left / 25 + (int32_t)TCS[cfg.tc].inc_s * 750;
  if (t > left / 3) t = left / 3;
  if (t < 50) t = 50;
  if (level_ms && (uint32_t)t > level_ms) t = (int32_t)level_ms;
  return (uint32_t)t;
}

static Result run_search(int depth, uint32_t ms, int noise) {
  Limits lim;
  lim.depth = depth;
  lim.time_ms = ms;
  lim.noise = noise;
  lim.seed = rnd();
  lim.max_nodes = 0;
  lim.now = now_ms;
  lim.poll = poll_cb;
  lim.info = info_cb;
  thinking = 1;
  last_draw = now_ms();
  strcpy(status, "Thinking...");
  redraw();
  uint32_t t0 = now_ms();
  Result r = search(CUR, G->keys, G->n + 1, &lim);
  last_think_ms = now_ms() - t0;
  last_nps = last_think_ms ? (uint32_t)((uint64_t)r.nodes * 1000 / last_think_ms) : r.nodes;
  last_res = r;
  thinking = 0;
  return r;
}

/* Small opening book (classic chess only, levels 4+): gives variety without
   randomising the search itself. Castling written king-to-g/c-file. */
static const char *const BOOK[] = {
  "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6",
  "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4",
  "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 d4c6 b7c6",
  "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5",
  "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1e3 e7e5",
  "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5",
  "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 d8c7",
  "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7 e4e5 f6d7",
  "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6",
  "e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6",
  "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8",
  "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5",
  "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5",
  "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8 f1e2 e7e5",
  "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7 f1g2 f8e7",
  "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 d7d5",
  "c2c4 g8f6 b1c3 e7e6 e2e4 d7d5",
  "g1f3 d7d5 g2g3 g8f6 f1g2 e7e6 e1g1 f8e7 d2d3 e8g8",
  "g1f3 g8f6 c2c4 c7c5 b1c3 b8c6",
};

static uint64_t classic_key;   /* key of the standard start position */

static Move book_move(void) {
  if (chess960 || G->pos960 >= 0 || G->hist[0].key != classic_key || G->n >= 14) return 0;
  /* game so far as "e2e4 e7e5 ..." (castles in king-destination form) */
  char line[200], *o = line;
  for (int i = 0; i < G->n; i++) {
    Move m = G->moves[i];
    move_to_str(&G->hist[i], m, o);
    if (MFLAGS(m) & F_CASTLE) { o[2] = (char)('a' + FILE_OF(MTO(m))); o[3] = (char)('1' + RANK_OF(MTO(m))); }
    o += 4; if (*o) o++;
    *o++ = ' ';
  }
  *o = 0;
  int len = (int)strlen(line);
  char cand[32][6];
  int nc = 0;
  for (unsigned i = 0; i < sizeof BOOK / sizeof *BOOK && nc < 32; i++) {
    const char *b = BOOK[i];
    if (strncmp(b, line, (size_t)len) || strlen(b) < (size_t)len + 4) continue;
    memcpy(cand[nc], b + len, 4);
    cand[nc][4] = 0;
    nc++;
  }
  if (!nc) return 0;
  return str_to_move(CUR, cand[rnd() % (uint32_t)nc]);
}

static void cpu_move(void) {
  Move m;
  const Level *L = &LEVELS[cfg.level];
  if (cfg.level >= 4 && (m = book_move()) != 0) {
    sys_delay(200);
    do_move(m);
    sprintf(status, "Book: %s", G->san[G->n - 1]);
    return;
  }
  if (cfg.level == 0) {
    m = G->legal[rnd() % (uint32_t)G->nlegal];
    sys_delay(250);
  } else {
    uint32_t ms = think_time(L->ms);
    int depth = L->depth;
    if (cfg.tc && !L->ms) depth = L->depth;     /* fixed-depth levels stay weak */
    Result r = run_search(depth, ms, L->noise);
    m = r.best ? r.best : G->legal[0];
  }
  do_move(m);
  if (cfg.level == 0 && !G->over) sprintf(status, "CRAZY played %s", G->san[G->n - 1]);
}

static void hint(void) {
  if (G->over || !G->nlegal) return;
  if (VS_USB) { strcpy(status, "No hints vs USB"); return; }   /* the opponent may be a person online */
  Result r = run_search(40, 1500, 0);
  hint_from = MFROM(r.best);
  hint_to = (MFLAGS(r.best) & F_CASTLE) ? castle_rook_sq(CUR, r.best) : MTO(r.best);
  char san[10];
  move_to_san(CUR, r.best, san);
  sprintf(status, "Hint: %s", san);
}

/* ------------------------------------------------------------------ */
/*  Files                                                             */
/* ------------------------------------------------------------------ */
static int write_file(const char *path, const char *data) {
  if (!sys_disk_ok()) return 0;
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f;
  UINT bw = 0;
  int ok = f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
  if (ok) {
    ok = f_write(&f, data, (UINT)strlen(data), &bw) == FR_OK && bw == strlen(data);
    f_close(&f);
  }
  sys_disk_write_enable(0);
  return ok;
}

static int read_file(const char *path, char *buf, int max) {
  if (!sys_disk_ok() || !file_exists(path)) return 0;
  FIL f;
  UINT br = 0;
  if (f_open(&f, path, FA_READ) != FR_OK) return 0;
  f_read(&f, buf, (UINT)(max - 1), &br);
  f_close(&f);
  buf[br] = 0;
  return (int)br;
}

static void serialize(char *o) {
  char fen[100];
  pos_to_fen(&G->hist[0], fen);
  o += sprintf(o, "CHESS47 1\n%s\n%d %d %d %d %d %d %d\n", fen, G->pos960, cfg.level, cfg.mode,
               cfg.human, cfg.flip, cfg.tc, chess960);
  o += sprintf(o, "%ld %ld\n", (long)clock_left(WHITE), (long)clock_left(BLACK));
  for (int i = 0; i < G->n; i++) {
    move_to_str(&G->hist[i], G->moves[i], o);
    o += strlen(o);
    *o++ = (i % 12 == 11) ? '\n' : ' ';
  }
  *o++ = '\n';
  *o = 0;
}

static int deserialize(char *s) {
  char *line[5] = { 0 };
  int n = 0;
  line[n++] = s;
  for (char *c = s; *c && n < 5; c++) if (*c == '\n') { *c = 0; line[n++] = c + 1; }
  if (n < 5 || strncmp(line[0], "CHESS47", 7)) return 0;
  int pos960, lv, mode, human, flip, tc, c960 = 0;
  if (sscanf(line[2], "%d %d %d %d %d %d %d", &pos960, &lv, &mode, &human, &flip, &tc, &c960) < 6) return 0;
  long cw = 0, cb = 0;
  sscanf(line[3], "%ld %ld", &cw, &cb);
  chess960 = c960;
  Pos p;
  if (!pos_from_fen(&p, line[1])) return 0;
  chess960 = c960;
  if (lv >= 0 && lv <= 9) cfg.level = (int8_t)lv;
  if (mode >= 0 && mode < MODE_CNT) cfg.mode = (int8_t)mode;
  cfg.human = (int8_t)(human & 1);
  cfg.flip = (int8_t)(flip & 1);
  if (tc >= 0 && tc < TC_CNT) cfg.tc = (int8_t)tc;
  new_game_from(&p, pos960);
  /* replay moves (rest of file, whitespace separated) */
  char *t = line[4];
  while (*t) {
    while (*t == ' ' || *t == '\n' || *t == '\r') t++;
    if (!*t) break;
    char mv[8]; int k = 0;
    while (*t && *t != ' ' && *t != '\n' && *t != '\r' && k < 6) mv[k++] = *t++;
    mv[k] = 0;
    Move m = str_to_move(CUR, mv);
    if (!m) break;
    int saved = clk_live; clk_live = 0;
    do_move(m);
    clk_live = saved;
  }
  if (cfg.tc) { clk[WHITE] = (int32_t)cw; clk[BLACK] = (int32_t)cb; }
  turn_start = now_ms();
  clock_update_live();
  return 1;
}

static void save_game(const char *path) {
  serialize(G->fbuf);
  strcpy(status, write_file(path, G->fbuf) ? "Saved" : "Save failed");
}

static int load_game(const char *path) {
  if (!read_file(path, G->fbuf, sizeof G->fbuf)) return 0;
  return deserialize(G->fbuf);
}

/* Append formatted text without writing past the destination buffer.
   On failure, leave a terminated, partial string and return 0. */
static int appendf(char **out, size_t *remaining, const char *fmt, ...) {
  if (*remaining == 0) return 0;

  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(*out, *remaining, fmt, ap);
  va_end(ap);

  if (n < 0 || (size_t)n >= *remaining) {
    (*out)[*remaining - 1] = '\0';
    *remaining = 0;
    return 0;
  }

  *out += n;
  *remaining -= (size_t)n;
  return 1;
}

/* Returns 0 if the complete PGN does not fit in cap bytes, including '\0'. */
static int build_pgn(char *o, size_t cap, int with_tags) {
  if (cap == 0) return 0;
  *o = '\0';
  size_t remaining = cap;

  if (with_tags) {
    tm_t tm; dt_t dt;
    rtc_read(&tm, &dt);
    char fen[100];
    pos_to_fen(&G->hist[0], fen);
    char lvl[24];
    sprintf(lvl, "Chess 47 L%s", LEVELS[cfg.level].name);
    const char *w = cfg.mode == MODE_HH || (cfg.mode == MODE_HC && cfg.human == WHITE) ? "Human" : lvl;
    const char *b = cfg.mode == MODE_HH || (cfg.mode == MODE_HC && cfg.human == BLACK) ? "Human" : lvl;
    if (!appendf(&o, &remaining,
                 "[Event \"Chess 47 on DM42n\"]\n[Site \"SwissMicros DM42n\"]\n"
                 "[Date \"%04d.%02d.%02d\"]\n[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n",
                 dt.year, dt.month, dt.day, w, b, result_tag())) return 0;
    if (chess960 || G->hist[0].fullmove != 1 || G->hist[0].side != WHITE)
      if (!appendf(&o, &remaining, "%s[SetUp \"1\"]\n[FEN \"%s\"]\n",
                   chess960 ? "[Variant \"Chess960\"]\n" : "", fen)) return 0;
    if (cfg.tc)
      if (!appendf(&o, &remaining, "[TimeControl \"%lu+%lu\"]\n",
                   (unsigned long)TCS[cfg.tc].base_s,
                   (unsigned long)TCS[cfg.tc].inc_s)) return 0;
    if (!appendf(&o, &remaining, "\n")) return 0;
  }

  int col = 0;
  for (int i = 0; i < G->n; i++) {
    const Pos *p = &G->hist[i];
    char tok[20];
    int n;
    if (p->side == WHITE) n = snprintf(tok, sizeof tok, "%d. %s", p->fullmove, G->san[i]);
    else if (i == 0) n = snprintf(tok, sizeof tok, "%d... %s", p->fullmove, G->san[i]);
    else n = snprintf(tok, sizeof tok, "%s", G->san[i]);
    if (n < 0 || (size_t)n >= sizeof tok) return 0;

    int len = n;
    if (with_tags && col + len > 78) {
      if (!appendf(&o, &remaining, "\n")) return 0;
      col = 0;
    } else if (col) {
      if (!appendf(&o, &remaining, " ")) return 0;
      col++;
    }
    if (!appendf(&o, &remaining, "%s", tok)) return 0;
    col += len;
  }

  return appendf(&o, &remaining, "%s%s\n", col ? " " : "", result_tag());
}

static void export_pgn(void) {
  if (!build_pgn(G->fbuf, sizeof G->fbuf, 1)) {
    strcpy(status, "PGN too long");
    return;
  }
  strcpy(status, write_file(DIR "/GAME.PGN", G->fbuf) ? "PGN saved" : "PGN failed");
}



/* ------------------------------------------------------------------ */
/*  Extra screens                                                     */
/* ------------------------------------------------------------------ */
static void wait_exit(void) {
  for (;;) {
    while (key_empty()) { CLR_ST(STAT_RUNNING); sys_sleep(); SET_ST(STAT_RUNNING); }
    int k = key_pop();
    if (k == KEY_EXIT || k == KEY_ENTER || k == KEY_F6) break;
  }
  wait_for_key_release(-1);
}

static void show_qr(void) {
/* QR unavailable in demo */
  if (cfg.mode == MODE_CC) {
    strcpy(status, "QR unavailable in demo");
    return;
  }

  /* moves only (plus FEN for 960) keeps the code small and scannable */
  char *txt = G->fbuf;
  char *o = txt;
  size_t remaining = sizeof G->fbuf;
  int text_ok = 1;
  if (chess960 || G->hist[0].fullmove != 1) {
    char fen[100];
    pos_to_fen(&G->hist[0], fen);
    text_ok = appendf(&o, &remaining, "[FEN \"%s\"]\n", fen);
  }
  if (text_ok) text_ok = build_pgn(o, remaining, 0);

  int len = text_ok ? (int)strlen(txt) : 0;
  QRCode qr;
  uint8_t *buf = NULL;
  int ok = -1;
  for (int v = 1; text_ok && v <= 40; v++) {
    uint16_t bs = qrcode_getBufferSize((uint8_t)v);
    buf = malloc(bs);
    if (!buf) break;
    ok = qrcode_initText(&qr, buf, (uint8_t)v, ECC_LOW, txt);
    if (ok == 0) break;
    free(buf); buf = NULL;
  }
  lcd_clear_buf();
  if (ok == 0 && buf) {
    int z = (LCD_Y - 16) / qr.size;
    if (z < 1) z = 1;
    int px = qr.size * z;
    qrcode_disp(&qr, 8, (LCD_Y - px) / 2, z);
    char s[40];
    int tx = px + 24;
    text_at(t24, tx, 20, "Game QR", 1);
    sprintf(s, "%d moves", (G->n + 1) / 2);
    text_at(t20, tx, 60, s, 0);
    sprintf(s, "%d chars  v%d", len, qr.version);
    text_at(t20, tx, 84, s, 0);
    text_at(t20, tx, 120, "Scan with phone,", 0);
    text_at(t20, tx, 142, "paste in lichess", 0);
    text_at(t20, tx, 164, "analysis board.", 0);
    text_at(t20, tx, 210, "EXIT to return", 0);
  } else {
    text_at(t24, 20, 100, "Game too long for QR", 0);
  }
  if (buf) free(buf);
  lcd_show();
  wait_exit();
}

static void show_info(void) {
  char s[64];
  lcd_clear_buf();
  int y = 4, h = lh(t20) + 1;
  text_at(t24, 4, y, "CHESS 47  v" PROGRAM_VERSION, 1);
  y += lh(t24) + 6;
  text_at(t20, 4, y, "Native DMCP5 chess for the DM42n", 0); y += h;
  text_at(t20, 4, y, "(C) 2026 Victor Valdes - MIT", 0); y += h + 6;
  sprintf(s, "Hash table: %u KB", (unsigned)(eng_tt_bytes() / 1024)); text_at(t20, 4, y, s, 0); y += h;
  sprintf(s, "Free heap: %d KB  (largest %d KB)", sys_free_mem() / 1024, sys_largest_free_mem() / 1024);
  text_at(t20, 4, y, s, 0); y += h;
  sprintf(s, "CPU: %s", usb_powered() ? "160 MHz (USB power)" : "80 MHz (battery)"); text_at(t20, 4, y, s, 0); y += h;
  if (last_res.nodes) {
    sprintf(s, "Last search: d%d  %lu nodes  %lu ms", last_res.depth, (unsigned long)last_res.nodes, (unsigned long)last_think_ms);
    text_at(t20, 4, y, s, 0); y += h;
    sprintf(s, "Speed: %lu nodes/s", (unsigned long)last_nps); text_at(t20, 4, y, s, 0); y += h;
  }
  y += 6;
  text_at(t20, 4, y, "Keys: 8/2/4/6 7/9/1/3 move cursor", 0); y += h;
  text_at(t20, 4, y, "ENTER or 5 select/move  <- undo", 0); y += h;
  text_at(t20, 4, y, "EXIT cancel / move now  SHIFT EXIT off", 0);
  lcd_show();
  wait_exit();
}

/* ------------------------------------------------------------------ */
/*  Setup menu (SHIFT 0) with system items                            */
/* ------------------------------------------------------------------ */
#define MI_C47_ABOUT 1
static const uint8_t mid_menu[] = {
  MI_SET_TIME, MI_SET_DATE, MI_BEEP_MUTE, MI_SLOW_AUTOREP, MI_MSC, MI_SYSTEM_ENTER, MI_C47_ABOUT, 0 };
static const smenu_t MID_MENU = { "Chess 47 Setup", mid_menu, NULL, NULL };

static int run_menu_item(uint8_t id) {
  if (id == MI_C47_ABOUT) { show_info(); return 0; }
  return MRET_UNIMPL;
}
static const char *menu_line_str(uint8_t id, char *s, const int slen) {
  (void)s; (void)slen;
  return id == MI_C47_ABOUT ? "About Chess 47 >" : NULL;
}

/* ------------------------------------------------------------------ */
/*  Input                                                             */
/* ------------------------------------------------------------------ */
static void move_cursor(int dx, int dy) {
  if (cfg.flip) { dx = -dx; dy = -dy; }
  int f = FILE_OF(cur) + dx, r = RANK_OF(cur) + dy;
  f = (f + 8) % 8;
  r = (r + 8) % 8;
  cur = SQ(f, r);
}

static int has_moves_from(int sq) {
  for (int i = 0; i < G->nlegal; i++) if (MFROM(G->legal[i]) == sq) return 1;
  return 0;
}

static void select_square(void) {
  if (G->over || !is_human(CUR->side)) return;
  int pc = CUR->b[cur];
  if (sel == NOSQ) {
    if (pc && PCOLOR(pc) == CUR->side && has_moves_from(cur)) sel = cur;
    else beep(220, 40);
    return;
  }
  if (cur == sel) { sel = NOSQ; return; }
  /* collect moves sel -> cur (castling: click rook, or king target if unique) */
  Move cand[4];
  int nc = 0, normal_to = 0;
  for (int i = 0; i < G->nlegal; i++) {
    Move m = G->legal[i];
    if (MFROM(m) == sel && MTO(m) == cur && !(MFLAGS(m) & F_CASTLE)) normal_to = 1;
  }
  for (int i = 0; i < G->nlegal && nc < 4; i++) {
    Move m = G->legal[i];
    if (MFROM(m) != sel) continue;
    if (MFLAGS(m) & F_CASTLE) {
      if (castle_rook_sq(CUR, m) == cur || (MTO(m) == cur && !normal_to)) cand[nc++] = m;
    } else if (MTO(m) == cur) cand[nc++] = m;
  }
  if (!nc) {
    if (pc && PCOLOR(pc) == CUR->side && has_moves_from(cur)) sel = cur;
    else beep(220, 40);
    return;
  }
  if (nc > 1 && MPROMO(cand[0])) {
    promo_mode = 1;
    for (int i = 0; i < 4; i++) promo_opts[i] = 0;
    for (int i = 0; i < nc; i++) {
      int pr = MPROMO(cand[i]);
      promo_opts[pr == QUEEN ? 0 : pr == ROOK ? 1 : pr == BISHOP ? 2 : 3] = cand[i];
    }
    return;
  }
  do_move(cand[0]);
}

static void start_960_entry(void) { num_mode = 1; num_len = 0; num_buf[0] = 0; }

static void finish_960_entry(void) {
  num_mode = 0;
  int n = num_len ? atoi(num_buf) : (int)(rnd() % 960);
  if (n > 959) { strcpy(status, "960 # must be 0-959"); return; }
  new_game(n);
  sprintf(status, "Chess960 #%d", n);
}

static void handle_softkey(int k) {
  int fn = k - KEY_F1;
  if (promo_mode) {
    if (fn < 4 && promo_opts[fn]) { promo_mode = 0; do_move(promo_opts[fn]); }
    else if (fn == 5) { promo_mode = 0; sel = NOSQ; }
    return;
  }
  switch (cfg.page * 6 + fn) {
  case 0:  new_game(G->pos960 >= 0 ? (int)(rnd() % 960) : -1); break;               /* NEW */
  case 1:  cfg.level = (int8_t)((cfg.level + 1) % 10);                              /* LEVEL */
           sprintf(status, "Level %s", LEVELS[cfg.level].name); break;
  case 2:  cfg.flip ^= 1; break;                                                     /* FLIP */
  case 3:  undo(); break;                                                            /* UNDO */
  case 4:  hint(); break;                                                            /* HINT */
  case 6: if (chess960) { new_game(-1); strcpy(status, "Normal chess");} 
          else {  start_960_entry(); } break; /* 960 / normal-chess toggle */                                               /* 960 */
  case 7:  cfg.tc = (int8_t)((cfg.tc + 1) % TC_CNT); clock_reset(); clock_update_live(); /* CLOCK */
           sprintf(status, "Clock %s", TCS[cfg.tc].name); break;
  case 8:  cfg.human ^= 1; cfg.flip = cfg.human; if (!VS_USB) cfg.mode = MODE_HC; sel = NOSQ;    /* SIDE */
           usb_opponent();
           sprintf(status, cfg.mode == MODE_EUSB ? "CPU plays %s" : "You play %s", cfg.human ? "Black" : "White"); break;
  case 9:  save_game(DIR "/GAME.TXT"); break;                                         /* SAVE */
  case 10: strcpy(status, load_game(DIR "/GAME.TXT") ? "Loaded" : "No saved game"); break; /* LOAD */
  case 12: export_pgn(); break;                                                      /* PGN */
  case 13: cfg.mode = (int8_t)((cfg.mode + 1) % MODE_CNT); sel = NOSQ;              /* MODE */
           strcpy(status, MODE_NAME[cfg.mode]);
           if (VS_USB) {
             if (!cfg.usb) {                                 /* vs USB needs the port: switch it on */
               if (!usbser_start("Chess 47")) { cfg.usb = 1; usbview_on(); usb_send("\r\nCHESS47 hello\r\n"); }
               else strcpy(status, "vs USB: connect USB");
             }
             usb_opponent();
           }
           break;
  case 14: show_qr(); break;                                                         /* QR */
  case 15: cfg.usb ^= 1;                                                             /* USB */
           if (cfg.usb) {
             int r = usbser_start("Chess 47");               /* our own serial port (TinyUSB) */
             if (!r) { usbview_on(); strcpy(status, "USB serial ON"); usb_send("\r\nCHESS47 hello\r\n"); }
             else { cfg.usb = 0; strcpy(status, r == -1 ? "Connect USB first" : "No memory for USB"); }
           } else { usbview_off(); usbser_stop(); strcpy(status, "USB serial OFF"); }
           break;
  case 16: show_info(); break;                                                       /* INFO */
  case 5: case 11: case 17: cfg.page = (int8_t)((cfg.page + 1) % 3); break;          /* >> */
  }
}

static void handle_key(int key) {
  if (key == KEY_SHIFT) { shift = !shift; return; }

  if (num_mode) {
    if (key >= KEY_F1 && key <= KEY_F6) { num_mode = 0; }
    else if (key == KEY_EXIT) { num_mode = 0; }
    else if (key == KEY_ENTER) { finish_960_entry(); }
    else if (key == KEY_BSP) { if (num_len) num_buf[--num_len] = 0; }
    else {
      int d = -1;
      static const int dk[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
      for (int i = 0; i < 10; i++) if (key == dk[i]) d = i;
      if (d >= 0 && num_len < 3) { num_buf[num_len++] = (char)('0' + d); num_buf[num_len] = 0; }
    }
    shift = 0;
    return;
  }

  if (shift) {
    shift = 0;
    switch (key) {
    case KEY_EXIT: SET_ST(STAT_PGM_END); return;                       /* OFF */
    case KEY_0:                                                         /* SETUP */
      SET_ST(STAT_MENU);
      handle_menu(&MID_MENU, MENU_RESET, 0);
      CLR_ST(STAT_MENU);
      wait_for_key_release(-1);
      return;
    case KEY_BSP: /* resign */
      if (!G->over && G->n) { G->over = 7; clk_live = 0; }
      return;
    case KEY_SIGMA: show_info(); return;
    }
  }

  status[0] = 0;
  if (key >= KEY_F1 && key <= KEY_F6) { handle_softkey(key); return; }
  if (promo_mode) { if (key == KEY_EXIT) { promo_mode = 0; sel = NOSQ; } return; }

  switch (key) {
  case KEY_UP:   case KEY_8: move_cursor(0, 1); break;
  case KEY_DOWN: case KEY_2: move_cursor(0, -1); break;
  case KEY_4: move_cursor(-1, 0); break;
  case KEY_6: move_cursor(1, 0); break;
  case KEY_7: move_cursor(-1, 1); break;
  case KEY_9: move_cursor(1, 1); break;
  case KEY_1: move_cursor(-1, -1); break;
  case KEY_3: move_cursor(1, -1); break;
  case KEY_5: case KEY_ENTER: select_square(); break;
  case KEY_EXIT: sel = NOSQ; hint_from = hint_to = NOSQ; break;
  case KEY_BSP: undo(); break;
  default: break;
  }
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */
static void *heap_alloc(size_t n) { return malloc(n); }

/* newlib's abort() needs _exit; the SDK syscalls don't provide one */
void _exit(int code) { (void)code; sys_reset(); for (;;) {} }

static void fatal(const char *msg) {
  lcd_clear_buf();
  text_at(t24, 10, 80, "CHESS 47", 1);
  text_at(t20, 10, 120, msg, 0);
  text_at(t20, 10, 150, "Load another program from DMCP menu", 0);
  lcd_show();
  for (;;) { wait_for_key_press(); }
}

static void program_init(void) {
  run_menu_item_app = run_menu_item;
  menu_line_str_app = menu_line_str;
  switch_usb_powered_freq();

  G = malloc(sizeof(Game));
  if (!G) fatal("Not enough memory for game");
  /* give the hash table what is left, keeping a reserve for the system */
  int largest = sys_largest_free_mem();
  int tt = largest - 160 * 1024;
  if (tt > 1024 * 1024) tt = 1024 * 1024;
  if (tt < 32 * 1024) tt = 32 * 1024;
  if (!eng_init(heap_alloc, (size_t)tt)) fatal("Not enough memory for engine");

  rng ^= sys_current_ms() * 2654435761u;
  rng ^= (uint32_t)get_rtc_ticks();
  if (!rng) rng = 47;

  new_game(-1);
  classic_key = G->hist[0].key;
  if (load_game(DIR "/AUTO.TXT")) strcpy(status, "Game resumed");
}

/* lines from the PC over USB: in "vs USB" mode the opponent's move in UCI (e7e5, e7e8q; castling king to rook
   square or to its own square, as str_to_move), or "msg <text>" for the status line. 1 if the screen changed. */
static char rxl[48];
static int rxn;
static int usb_line(const char *l) {
  if (!strncmp(l, "msg ", 4)) { snprintf(status, sizeof status, "%.36s", l + 4); return 1; }
  int bot = !strncmp(l, "botgame ", 8);
  if (bot || !strncmp(l, "newgame ", 8)) {            /* the PC starts a game: "newgame white|black [FEN]" (your side), */
    Pos p;                                             /* "botgame white|black [FEN]" (the engine's side, held until go) */
    const char *f = strchr(l + 8, ' ');
    chess960 = 0;
    if (!f || !pos_from_fen(&p, f + 1)) pos_start(&p);
    cfg.mode = bot ? MODE_EUSB : MODE_USB;
    cfg.human = (int8_t)(strncmp(l + 8, "black", 5) ? WHITE : BLACK);
    cfg.flip = cfg.human;
    if (bot) { cfg.tc = 0; usb_hold = 1; usb_left = -1; usb_inc = 0; }   /* the time comes from the PC (clock) */
    sel = NOSQ;
    new_game_from(&p, -1);
    return 1;
  }
  if (!strcmp(l, "go")) { usb_hold = 0; return 1; }   /* CPU vs USB: the board is caught up, the engine may move */
  if (!strncmp(l, "level ", 6)) {                      /* "level 0..9" */
    int v = atoi(l + 6);
    if (v >= 0 && v <= 9) { cfg.level = (int8_t)v; sprintf(status, "Level %s", LEVELS[cfg.level].name); }
    return 1;
  }
  if (!strncmp(l, "clock ", 6)) {                      /* "clock <engine ms left> <increment ms>" */
    long t = 0, i = 0;
    sscanf(l + 6, "%ld %ld", &t, &i);
    usb_left = (int32_t)t; usb_inc = (int32_t)i;
    return 0;
  }
  if (!strncmp(l, "end", 3) && (!l[3] || l[3] == ' ')) {   /* the game ended on the PC's side: "end <text>" */
    if (!G->over) { G->over = 8; clk_live = 0; }
    snprintf(end_txt, sizeof end_txt, "%.20s", l[3] ? l + 4 : "");
    return 1;
  }
  if (!strncmp(l, "play ", 5)) {                       /* a move of either side (the PC catching the board up) */
    Move m = G->over ? 0 : str_to_move(CUR, l + 5);
    if (!m) { char s[64]; snprintf(s, sizeof s, "illegal %.12s\r\n", l + 5); usb_send(s); return 0; }
    do_move(m);
    return 1;
  }
  if (!VS_USB) return 0;
  if (G->over) { usb_send("over\r\n"); return 0; }
  if (!usb_side(CUR->side)) { usb_send("notyourturn\r\n"); return 0; }
  Move m = str_to_move(CUR, l);
  if (!m) { char s[64]; snprintf(s, sizeof s, "illegal %.12s\r\n", l); usb_send(s); return 0; }
  do_move(m);                                          /* echoed to the PC: it confirms the move */
  if (!G->over) snprintf(status, sizeof status, "USB played %s", G->san[G->n - 1]);
  return 1;
}
static int usb_poll(void) {
  char b[64];
  int n = usbser_read(b, sizeof b), changed = 0;
  for (int i = 0; i < n; i++) {
    char c = b[i];
    if (c == '\r' || c == '\n') {
      if (rxn) { rxl[rxn] = 0; changed |= usb_line(rxl); rxn = 0; }
    } else if (rxn < (int)sizeof rxl - 1 && c >= 32 && c < 127) rxl[rxn++] = c;
  }
  return changed;
}

void program_main(void) {
  program_init();
  redraw();

  for (;;) {
    /* computer to move? (only while awake and no key waiting) */
    if (deferred_key > 0) {
      int k = deferred_key;
      deferred_key = -1;
      handle_key(k);
      redraw();
      continue;
    }
    if (cfg.usb && usb_poll()) { redraw(); continue; }        /* a line from the PC (vs USB: its move) */
    if (cfg.mode != MODE_USB && !(cfg.mode == MODE_EUSB && (usb_hold || usb_side(CUR->side))) &&
        !ST(STAT_PGM_END) && !ST(STAT_SUSPENDED) && !G->over && !is_human(CUR->side) &&
        !promo_mode && !num_mode && key_empty()) {
      cpu_move();
      redraw();
      continue;
    }

    if (cfg.usb && !ST(STAT_PGM_END) && key_empty())
      usbser_idle(250);                                    /* the USB runs: no sleep (it would stop the USB) */
    else if ((ST(STAT_PGM_END) && ST(STAT_SUSPENDED)) || (!ST(STAT_PGM_END) && key_empty())) {
      if (clk_live) sys_timer_start(0, 250);
      CLR_ST(STAT_RUNNING);
      sys_sleep();
    }

    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) {
      if (!ST(STAT_SUSPENDED)) {
        /* going off: pause clock, autosave, draw off image */
        if (clk_live) { clk[CUR->side] = clock_left(CUR->side); clk_live = 0; }
        save_game(DIR "/AUTO.TXT");
        if (cfg.usb) { usbview_off(); usbser_stop(); cfg.usb = 0; }
        lcd_set_buf_cleared(0);
        draw_power_off_image(1);
        LCD_power_off(0);
        SET_ST(STAT_SUSPENDED);
        SET_ST(STAT_OFF);
      }
      continue;
    }

    SET_ST(STAT_RUNNING);
    CLR_ST(STAT_SUSPENDED);

    if (ST(STAT_OFF)) {
      LCD_power_on();
      rtc_wakeup_delay();
      CLR_ST(STAT_OFF);
      status[0] = 0;
      clock_update_live();
      redraw();
    }
    if (ST(STAT_POWER_CHANGE)) {
      switch_usb_powered_freq();
      CLR_ST(STAT_POWER_CHANGE);
    }

    /* clock tick */
    if (clk_live) {
      uint32_t t = now_ms();
      if (t - last_tick >= 250) {
        last_tick = t;
        reset_auto_off();
        if (clock_left(CUR->side) <= 0) {
          clk[CUR->side] = 0; G->over = 6; clk_live = 0; beep(330, 400);
          redraw();
        } else {
          draw_clock();
          lcd_refresh_lines(0, lh(t20) + 2); lcd_refresh_wait();
        }
      }
    }

    if (!key_empty()) reset_auto_off();
    int key = key_pop();
    if (key > 0) {
      handle_key(key);
      redraw();
    }
  }
}
