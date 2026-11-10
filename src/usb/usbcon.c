/* usbcon.c - DMCP5 3.58's serial console (IFC 3.18) played over our own TinyUSB port (make USB=tinyusb), so the
 * program and the PC tools are the same with any DMCP5: src/sercon.c calls usb_serial_ioctl / _read_line / _write
 * here instead of DMCP's library slots, and the PC sees the same byte stream (SwissMicros' preliminary data sheet
 * "DMCP5 Serial Console API", 2026-11-06):
 *
 *   a byte 0x10 + n switches the stream to channel n (0..10); lines end in '\n'; a line is an ASCII part, then a
 *   binary part of bytes with the top bit set (7 bits each, least significant first)
 *   channel 0 (DMCP's, served here):  V  KPhh[hh]  KR  K hh  L  B   from the PC;
 *                                      "L <yy>" + the 400 pixels of a changed LCD row, the V answer
 *   channel 1: printf (SERIO_SEND_PRINTF); channels 2, 3: the program's own, both ways; 4..10: out only
 *
 * The LCD rows: the line buffer's layout is learnt once (one pixel set and cleared, its old bytes put back), then
 * each row is written as DMCP sends it: yy = 239 for the top screen row, pixel x in byte 49 - x/8, bit 7 - x%8,
 * a dark pixel is a 0 bit. A row goes out when it differs from the one last sent (also counted as sent when no PC
 * listens); after L every row is unsent, the program gets key 77, redraws, and the non-blank rows go out.
 *
 * Differences from DMCP (on purpose): B (DFU) is ignored; the identity says "DMCP5 TinyUSB"; usb_serial_write waits
 * while the PC reads (up to 0.3 s) instead of losing bytes; KPhhhh injects only the first key; no key reports
 * (SERIO_SEND_KEYS is kept but sends nothing: DMCP5 gives programs no call to read the keys held).
 * The state (input FIFOs, row checksums, about 2.3 KB) is on the heap while the port is on: DMCP5 has 8 KB of static RAM.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sercon_ifc.h"
#include "tport.h"

#define NIN      4                        /* input FIFOs (DMCP: USIN_CHAN_CNT); 2 and 3 are the program's */
#define NOUT     11                       /* output channels 0..10 */
#define RB       (LCD_X / 8)              /* 50 bytes a row */
#define PACKED   58                       /* 400 bits, 7 a byte */
#define SCAN     54                       /* the whole line buffer (command and dummy bytes too) */

typedef struct {
  uint8_t  data[USIN_CHAN_SIZE];
  uint16_t len, plen;                     /* bytes held (whole lines + the partial one); the partial line's */
  uint8_t  lines, drop;                   /* whole lines held; dropping until the next '\n' */
  uint16_t llen[USIN_MAX_READLINES];      /* each line's length, its end ('\n' or 0x0f) included */
  uint8_t  lovf[USIN_MAX_READLINES];      /* cut by an overflow */
} Fifo;

static struct Con {
  Fifo in[NIN];
  uint32_t hash[LCD_Y];                   /* the row last sent (FNV-1a of its 50 wire bytes) */
  uint8_t sent[(LCD_Y + 7) / 8];          /* that row was sent (or counted as sent) */
} *C;

static int en, err, s_lcd, s_keys, s_printf, scan = -1, was_conn;
static uint8_t in_ch;
static const char *prod = "47 calculator", *ver = "";
static int lay_ok, fast, b0, bstep, k0, kstep, ink;   /* x -> byte b0 + bstep*(x/8), bit k0 + kstep*(x%8) */

void usbcon_ident(const char *product, const char *version) { prod = product; ver = version; }
int usbcon_error(void) { return err; }
int usbcon_layout_ok(void) { return lay_ok; }

/* ---------------------------------------------------------------- the LCD buffer's layout, learnt once */
static int find(int x, int y, int *byte, int *bit, int *val) {
  uint8_t a[SCAN], b[SCAN];
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 1);
  memcpy(a, lcd_line_addr(y), SCAN);
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 0);
  memcpy(b, lcd_line_addr(y), SCAN);
  for (int i = 0; i < SCAN; i++) {
    uint8_t d = (uint8_t)(a[i] ^ b[i]);
    if (!d) continue;
    for (int k = 0; k < 8; k++)
      if (d == (1u << k)) { *byte = i; *bit = k; *val = (a[i] >> k) & 1; return 1; }
    return 0;
  }
  return 0;
}

static void learn(void) {
  int y = LCD_Y - 1, bx, kx, v, b1, k1, v1, b8, k8, v8;
  uint8_t keep[SCAN];
  memcpy(keep, lcd_line_addr(y), SCAN);
  lay_ok = find(0, y, &bx, &kx, &v) && find(1, y, &b1, &k1, &v1) && find(8, y, &b8, &k8, &v8) && b1 == bx;
  memcpy(lcd_line_addr(y), keep, SCAN);                /* the pixels as they were */
  if (!lay_ok) return;
  b0 = bx; k0 = kx; ink = v; kstep = k1 - kx; bstep = b8 - bx;
  lay_ok = (kstep == 1 || kstep == -1) && (bstep == 1 || bstep == -1) && b0 + bstep * (RB - 1) >= 0 &&
           b0 + bstep * (RB - 1) < SCAN;
  fast = lay_ok && b0 == 2 + RB - 1 && bstep == -1 && k0 == 7 && kstep == -1 && ink == 0;   /* already DMCP's */
}

/* wire row w (0 = the bottom screen row) as DMCP sends it */
static void wire_row(int w, uint8_t *o) {
  const uint8_t *p = lcd_line_addr(LCD_Y - 1 - w);
  if (fast) { memcpy(o, p + 2, RB); return; }
  memset(o, 0xFF, RB);
  for (int x = 0; x < LCD_X; x++)
    if (((p[b0 + bstep * (x >> 3)] >> (k0 + kstep * (x & 7))) & 1) == ink)
      o[RB - 1 - (x >> 3)] &= (uint8_t)~(0x80u >> (x & 7));
}

/* ---------------------------------------------------------------- output */
static int put(uint8_t ch, const void *p, int n, int wait) {   /* one id byte, then the data, in one go */
  uint32_t t = sys_current_ms();
  while (tport_tx_room() < n + 1) {
    if (!wait || !tport_connected() || sys_current_ms() - t > 300) return 0;   /* the PC does not read */
    tport_task();
  }
  uint8_t id = (uint8_t)(US_CHAN0_CHAR + ch);
  tport_tx(&id, 1);
  tport_tx(p, n);
  return 1;
}

void usb_serial_write(uint8_t chan_nr, const char *buf, int len) {
  if (!en || chan_nr >= NOUT || len <= 0 || !tport_connected()) return;
  put(chan_nr, buf, len > 255 ? 255 : len, 1);             /* DMCP sends at most 255 bytes of a call */
}

/* ---------------------------------------------------------------- input: DMCP's FIFOs */
static void in_byte(Fifo *f, uint8_t b) {
  if (f->drop) { if (b == US_LINEEND_CHAR) f->drop = 0; return; }
  if (f->plen == 0 && (f->lines >= USIN_MAX_READLINES || f->len >= USIN_CHAN_SIZE)) {
    if (b != US_LINEEND_CHAR) f->drop = 1;                   /* no room for another line: it is lost */
    return;
  }
  if (b == US_LINEEND_CHAR || f->len >= USIN_CHAN_SIZE - 1) {
    uint8_t ovf = b != US_LINEEND_CHAR;                      /* the last free byte: the line ends in 0x0f */
    f->data[f->len++] = ovf ? US_OVERFLOW_CHAR : US_LINEEND_CHAR;
    f->llen[f->lines] = (uint16_t)(f->plen + 1);
    f->lovf[f->lines++] = ovf;
    f->plen = 0;
    f->drop = ovf;
    return;
  }
  f->data[f->len++] = b;
  f->plen++;
}

int usb_serial_read_line(uint8_t chan_nr, uint8_t *buf, int buflen, uint8_t *is_overflow) {
  if (!en || chan_nr >= NIN) return 0;
  Fifo *f = &C->in[chan_nr];
  if (!f->lines) return 0;
  int n = f->llen[0];
  if (buf) {
    if (n > buflen) return -1;                               /* it stays: discard it with buf = NULL */
    memcpy(buf, f->data, (size_t)n);
    buf[n] = 0;                                              /* the caller has buflen + 1 bytes (data sheet) */
    if (is_overflow) *is_overflow = f->lovf[0];
  }
  memmove(f->data, f->data + n, (size_t)(f->len - n));
  f->len = (uint16_t)(f->len - n);
  f->lines--;
  memmove(f->llen, f->llen + 1, f->lines * sizeof f->llen[0]);
  memmove(f->lovf, f->lovf + 1, f->lines);
  return n;
}

/* ---------------------------------------------------------------- switches */
static void port_on(void) {
  if (en) return;
  C = calloc(1, sizeof *C);
  if (!C) { err = -2; return; }
  err = tport_start(prod);
  if (err) { free(C); C = NULL; return; }
  en = 1; in_ch = 0; scan = -1; was_conn = 0;
  learn();
}

static void port_off(void) {
  if (!en) return;
  tport_stop();
  free(C); C = NULL;
  en = 0; scan = -1;
}

int usb_serial_ioctl(int op, int val) {
  int rd = val == SER_IOCTL_READ, *s;
  switch (op) {
  case SERIO_IS_ENABLED:
    if (!rd) { if (val) port_on(); else port_off(); }
    return en;
  case SERIO_IS_ACTIVE:    return en && tport_mounted();
  case SERIO_IS_CONNECTED: return en && tport_connected();
  case SERIO_LINE_READY:   return en && val >= 0 && val < NIN ? C->in[val].lines : 0;
  case SERIO_LINE_LENGTH:  return en && val >= 0 && val < NIN && C->in[val].lines ? C->in[val].llen[0] : 0;
  case SERIO_SEND_PRINTF:  s = &s_printf; break;
  case SERIO_SEND_LCD:     s = &s_lcd; break;
  case SERIO_SEND_KEYS:    s = &s_keys; break;           /* kept, but no key reports (see the top) */
  case SERIO_SEND_IR:      return 0;                         /* no IR printer over this port */
  default:                 return SER_IOCTL_INVALID;
  }
  if (!rd) *s = val ? 1 : 0;
  return *s;
}

/* ---------------------------------------------------------------- channel 0, as DMCP serves it */
static uint32_t fnv(const uint8_t *p) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < RB; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}

static void lcd_rows(void) {          /* the changed rows, top screen row first (as DMCP), while the FIFO has room */
  int conn = tport_connected();
  for (; scan >= 0 && scan < LCD_Y; scan++) {             /* scan: the screen row; w: the wire row */
    int w = LCD_Y - 1 - scan;
    uint8_t row[RB];
    wire_row(w, row);
    uint32_t h = fnv(row);
    int was = C->sent[w >> 3] >> (w & 7) & 1;
    if (was && h == C->hash[w]) continue;
    int blank = 1;
    for (int i = 0; i < RB && blank; i++) blank = row[i] == 0xFF;
    if (conn && (was || !blank)) {                           /* after L, blank rows stay unsent (as DMCP) */
      static const char HX[] = "0123456789ABCDEF";
      uint8_t b[4 + PACKED + 1];
      int n = 0;
      b[n++] = 'L'; b[n++] = ' '; b[n++] = (uint8_t)HX[w >> 4]; b[n++] = (uint8_t)HX[w & 15];
      uint32_t acc = 0;
      int bits = 0;
      for (int i = 0; i < RB; i++) {
        acc |= (uint32_t)row[i] << bits;
        for (bits += 8; bits >= 7; bits -= 7) { b[n++] = (uint8_t)(0x80 | (acc & 0x7F)); acc >>= 7; }
      }
      if (bits) b[n++] = (uint8_t)(0x80 | (acc & 0x7F));
      b[n++] = '\n';
      if (!put(0, b, n, 0)) return;                          /* the FIFO is full: the rest at the next poll */
    }
    C->hash[w] = h;
    C->sent[w >> 3] |= (uint8_t)(1u << (w & 7));
  }
  scan = -1;
}

void usbcon_lcd_refreshed(void) { if (en && s_lcd && lay_ok) scan = 0; }

static int hexval(const char *s, int *len) {
  int v = 0, n = 0;
  for (;; s++, n++) {
    int c = *s, d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) break;
    v = v * 16 + d;
  }
  *len = n;
  return v;
}

static void command(char *s) {
  int n;
  if (!strcmp(s, "V")) {
    char b[96];
    n = snprintf(b, sizeof b, "V|HW %s|VER %s|DMCP5 TinyUSB|IFC -|SN -\n", prod, ver);
    if (n > (int)sizeof b - 1) { n = (int)sizeof b - 1; b[n - 1] = '\n'; }
    put(0, b, n, 1);
  } else if (!strcmp(s, "L")) {                              /* the whole screen: every row unsent, key 77 */
    memset(C->sent, 0, sizeof C->sent);
    key_push(SERCON_KEY_REDRAW);
    key_push(0);
  } else if (s[0] == 'K' && s[1] == 'R') {
    key_push(0);
  } else if (s[0] == 'K') {                                  /* KPhh / KPhhhh held, K hh a press */
    int hold = s[1] == 'P';
    const char *p = s + 1 + hold;
    while (*p == ' ') p++;
    int v = hexval(p, &n), k = n > 2 ? v & 0xFF : v;         /* KPhhhh: k1 is the low byte */
    if (n == 0 || k <= 0) return;
    key_push(k);
    if (!hold) key_push(0);
  }                                                          /* B (DFU): not from this port */
}

/* ---------------------------------------------------------------- the pump */
void usbcon_poll(void) {
  if (!en) return;
  tport_task();
  int conn = tport_connected();
  if (conn && !was_conn) in_ch = 0;
  was_conn = conn;
  uint8_t b[64];
  for (int n; (n = tport_rx(b, sizeof b)) > 0;)
    for (int i = 0; i < n; i++) {
      if (b[i] >= US_CHAN0_CHAR && b[i] < US_CHAN0_CHAR + NOUT) in_ch = (uint8_t)(b[i] - US_CHAN0_CHAR);
      else if (in_ch < NIN) in_byte(&C->in[in_ch], b[i]);   /* 4..10 have no input FIFO */
    }
  char line[USIN_CHAN_SIZE + 1];
  uint8_t ovf;
  for (int n; (n = usb_serial_read_line(0, (uint8_t *)line, USIN_CHAN_SIZE, &ovf)) != 0;) {
    if (n < 0) { usb_serial_read_line(0, NULL, USIN_CHAN_SIZE, NULL); continue; }
    if (ovf || line[n - 1] != '\n') continue;
    line[n - 1] = 0;
    command(line);
  }
  while (usb_serial_read_line(1, NULL, USIN_CHAN_SIZE, NULL) != 0) {}   /* DMCP's system input: unused */
  if (scan >= 0) lcd_rows();
}

void usbcon_idle(uint32_t ms) {
  usbcon_poll();
  if (en) tport_wait(ms, usbcon_poll);
}

int usbcon_printf_on(void) { return en && s_printf; }
