/*
 * CHESS 47 PC simulator - links the real src/main.c + src/engine.c against
 * a fake DMCP5 layer and renders the 400x240 LCD.
 *
 * usage: sim KEY KEY ... SNAP:name ...
 *   keys: F1..F6 0..9 ENTER EXIT BSP UP DOWN SHIFT SIGMA, WAIT:ms (fake time)
 *   SNAP:name writes name.pbm (framebuffer) and name.txt (text draw list)
 * Text is drawn later by render.py with a real font; widths here are fixed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "dmcp.h"

sys_sdb_t sim_sdb;
static disp_stat_t ds20, ds24, dsReg;
static line_font_t f20 = { "t20", 9, 19, 15, 32, 96, 1, 1, 0, 0 };
static line_font_t f24 = { "t24", 11, 23, 18, 32, 96, 1, 1, 0, 0 };

static uint8_t fb[LCD_Y][LCD_X];
typedef struct { int font, x, y, inv; char s[64]; } Txt;
static Txt txt[600];
static int ntxt;

static char **script;
static int nscript, spos;
static uint32_t fake_ms;
static int real_time = 1;

/* ---------------- LCD ---------------- */
void lcd_clear_buf(void) { memset(fb, 0, sizeof fb); ntxt = 0; }
void lcd_refresh(void) {}
void lcd_refresh_wait(void) {}
void lcd_refresh_lines(int ln, int cnt) { (void)ln; (void)cnt; }
void lcd_forced_refresh(void) {}
void lcd_set_buf_cleared(int v) { (void)v; }
int  lcd_get_buf_cleared(void) { return 0; }
void LCD_power_on(void) {}
void LCD_power_off(int c) { (void)c; }
void draw_power_off_image(int a) { (void)a; }

static void kill_text_in(int x, int y, int w, int h) {
  /* text drawn earlier and now painted over disappears (like on the LCD) */
  for (int i = 0; i < ntxt; i++) {
    Txt *t = &txt[i];
    int th = t->font == 24 ? 23 : 19;
    if (t->x >= x && t->y >= y && t->x < x + w && t->y + th <= y + h + 2) t->s[0] = 0;
  }
}

void lcd_fill_rect(uint32_t x, uint32_t y, uint32_t dx, uint32_t dy, int val) {
  for (uint32_t j = y; j < y + dy && j < LCD_Y; j++)
    for (uint32_t i = x; i < x + dx && i < LCD_X; i++) fb[j][i] = (uint8_t)(val ? 1 : 0);
  if (dx > 8 && dy > 8) kill_text_in((int)x, (int)y, (int)dx, (int)dy);
}

static int fw(disp_stat_t *ds) { return ds->f->width; }
int lcd_lineHeight(disp_stat_t *ds) { return ds->f->height; }
int lcd_textWidth(disp_stat_t *ds, const char *t) { return (int)strlen(t) * fw(ds); }
void lcd_setXY(disp_stat_t *ds, int x, int y) { ds->x = (int16_t)x; ds->y = (int16_t)y; }
void lcd_writeText(disp_stat_t *ds, const char *s) {
  if (ntxt < 600) {
    Txt *t = &txt[ntxt++];
    t->font = ds->f == &f24 ? 24 : 20;
    t->x = ds->x; t->y = ds->y; t->inv = ds->inv;
    snprintf(t->s, sizeof t->s, "%s", s);
    if (ds->inv) for (int j = ds->y; j < ds->y + ds->f->height && j < LCD_Y; j++)
      for (int i = ds->x; i < ds->x + lcd_textWidth(ds, s) && i < LCD_X; i++) fb[j][i] = 1;
  }
  ds->x = (int16_t)(ds->x + lcd_textWidth(ds, s));
}
void lcd_draw_menu_keys(const char *keys[]) {
  int y0 = LCD_Y - LCD_MENU_LINES + 2, w = LCD_X / 6;
  lcd_fill_rect(0, LCD_Y - LCD_MENU_LINES, LCD_X, LCD_MENU_LINES, 0);
  for (int i = 0; i < 6; i++) {
    int x0 = i * w + 2;
    lcd_fill_rect((uint32_t)x0, (uint32_t)y0, (uint32_t)(w - 4), LCD_MENU_LINES - 4, 1);
    if (keys[i] && keys[i][0] && ntxt < 600) {
      Txt *t = &txt[ntxt++];
      t->font = 20; t->inv = 2; t->y = y0 + 5;
      t->x = x0 + (w - 4 - (int)strlen(keys[i]) * 9) / 2;
      snprintf(t->s, sizeof t->s, "%s", keys[i]);
    }
  }
}
void qrcode_disp(QRCode *qr, int xo, int yo, int z) { (void)qr; (void)xo; (void)yo; (void)z; }
uint16_t qrcode_getBufferSize(uint8_t v) { (void)v; return 64; }
int8_t qrcode_initText(QRCode *q, uint8_t *m, uint8_t v, uint8_t e, const char *d) { (void)q; (void)m; (void)v; (void)e; (void)d; return -1; }
bool qrcode_getModule(QRCode *q, uint8_t x, uint8_t y) { (void)q; (void)x; (void)y; return 0; }

/* ---------------- time / power ---------------- */
uint32_t sys_current_ms(void) {
  if (!real_time) return fake_ms;
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000) + fake_ms;
}
uint32_t get_rtc_ticks(void) { return 12345; }
void sys_delay(uint32_t ms) { (void)ms; }
void sys_timer_start(int ix, uint32_t ms) { (void)ix; (void)ms; }
void start_buzzer_freq(uint32_t f) { (void)f; }
void stop_buzzer(void) {}
int  switch_usb_powered_freq(void) { return 0; }
int  usb_powered(void) { return 1; }
void usb_acm_on(void) {}
int  usb_is_on(void) { return 1; }
void usb_turn_off(void) {}
static FILE *acm;
void acm_puts(const char *s) { if (!acm) acm = fopen("acm.out", "w"); fputs(s, acm); fflush(acm); }
/* the DMCP line buffer with its own oddities (offset, mirrored, LSB first, inverted), for usbview.c to learn */
uint8_t *lcd_line_addr(int y) {
  static uint8_t lb[LCD_Y][54];
  memset(lb[y], 0xFF, 54);
  for (int x = 0; x < LCD_X; x++)
    if (fb[y][x]) { int X = LCD_X - 1 - x; lb[y][2 + X / 8] &= (uint8_t)~(1u << (X % 8)); }
  return lb[y];
}
void reset_auto_off(void) {}
void rtc_wakeup_delay(void) {}
void rtc_read(tm_t *tm, dt_t *dt) { memset(tm, 0, sizeof *tm); dt->year = 2026; dt->month = 11; dt->day = 2; }
int  sys_free_mem(void) { return 600 * 1024; }
int  sys_largest_free_mem(void) { return 560 * 1024; }
void sys_reset(void) { exit(3); }
int  handle_menu(const smenu_t *m, int a, int l) { (void)m; (void)a; (void)l; return 0; }
void wait_for_key_release(int t) { (void)t; }

/* ---------------- files -> ./simdisk ---------------- */
static void path(const char *p, char *o) { snprintf(o, 256, "simdisk%s", p); }
int sys_disk_ok(void) { return 1; }
int sys_disk_write_enable(int v) { (void)v; return 1; }
int check_create_dir(const char *d) { char p[256], c[300]; path(d, p); snprintf(c, sizeof c, "mkdir -p %s", p); return system(c); }
int file_exists(const char *f) { char p[256]; path(f, p); FILE *x = fopen(p, "rb"); if (x) fclose(x); return x != NULL; }
FRESULT f_open(FIL *fp, const TCHAR *p, BYTE mode) {
  char q[256]; path(p, q);
  FILE *f = fopen(q, (mode & FA_WRITE) ? "wb" : "rb");
  fp->obj.fs = (FATFS *)f;
  return f ? FR_OK : FR_NO_FILE;
}
FRESULT f_close(FIL *fp) { fclose((FILE *)fp->obj.fs); return FR_OK; }
FRESULT f_read(FIL *fp, void *b, UINT n, UINT *br) { *br = (UINT)fread(b, 1, n, (FILE *)fp->obj.fs); return FR_OK; }
FRESULT f_write(FIL *fp, const void *b, UINT n, UINT *bw) { *bw = (UINT)fwrite(b, 1, n, (FILE *)fp->obj.fs); return FR_OK; }

/* ---------------- keys from the script ---------------- */
static void snap(const char *name) {
  char fn[256];
  snprintf(fn, sizeof fn, "%s.pbm", name);
  FILE *f = fopen(fn, "w");
  fprintf(f, "P1\n%d %d\n", LCD_X, LCD_Y);
  for (int y = 0; y < LCD_Y; y++) { for (int x = 0; x < LCD_X; x++) fputc(fb[y][x] ? '1' : '0', f); fputc('\n', f); }
  fclose(f);
  snprintf(fn, sizeof fn, "%s.txt", name);
  f = fopen(fn, "w");
  for (int i = 0; i < ntxt; i++) if (txt[i].s[0]) fprintf(f, "%d\t%d\t%d\t%d\t%s\n", txt[i].font, txt[i].x, txt[i].y, txt[i].inv, txt[i].s);
  fclose(f);
  fprintf(stderr, "snap %s\n", name);
}

static int pending = -1;
static int idle;   /* WAIT: keypad idle until the program sleeps */
static int parse_key(const char *s) {
  static const struct { const char *n; int k; } map[] = {
    {"F1",KEY_F1},{"F2",KEY_F2},{"F3",KEY_F3},{"F4",KEY_F4},{"F5",KEY_F5},{"F6",KEY_F6},
    {"0",KEY_0},{"1",KEY_1},{"2",KEY_2},{"3",KEY_3},{"4",KEY_4},{"5",KEY_5},{"6",KEY_6},{"7",KEY_7},{"8",KEY_8},{"9",KEY_9},
    {"ENTER",KEY_ENTER},{"EXIT",KEY_EXIT},{"BSP",KEY_BSP},{"UP",KEY_UP},{"DOWN",KEY_DOWN},{"SHIFT",KEY_SHIFT},{"SIGMA",KEY_SIGMA},{"ADD",KEY_ADD},{"SUB",KEY_SUB},
  };
  for (unsigned i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(map[i].n, s)) return map[i].k;
  return -1;
}
/* advance the script until a key is available (handles SNAP/WAIT) */
static void fill(void) {
  if (idle) return;
  while (pending < 0 && spos < nscript) {
    const char *t = script[spos++];
    if (!strncmp(t, "SNAP:", 5)) snap(t + 5);
    else if (!strncmp(t, "WAIT:", 5)) { fake_ms += (uint32_t)atoi(t + 5); idle = 1; return; }
    else pending = parse_key(t);
  }
}
int key_empty(void) { fill(); return pending < 0; }
int key_pop(void) { fill(); int k = pending; pending = -1; return k; }
void wait_for_key_press(void) { if (key_empty()) exit(0); key_pop(); }
void sys_sleep(void) { if (idle) { idle = 0; return; } if (key_empty()) { fprintf(stderr, "script done\n"); exit(0); } }

void program_main(void);
int main(int argc, char **argv) {
  ds20.f = &f20; ds24.f = &f24; dsReg.f = &f24;
  ds20.lnfill = ds24.lnfill = 1; ds20.newln = ds24.newln = 1;
  sim_sdb.pds_t20 = &ds20; sim_sdb.pds_t24 = &ds24; sim_sdb.pds_fReg = &dsReg;
  script = argv + 1;
  nscript = argc - 1;
  program_main();
  return 0;
}
