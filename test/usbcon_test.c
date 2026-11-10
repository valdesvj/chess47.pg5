/* usbcon_test.c - PC test of src/usb/usbcon.c (DMCP5 3.58's serial console over our own TinyUSB port), with a fake
 * port and a fake LCD whose line buffer is NOT DMCP's layout (so the learnt layout is really used):
 *   make test-usb
 * Checks: V answer, KP / KR / K key injection, L (key 77 + every non-blank row), the LCD row format against the data
 * sheet's (decoded the way tools/dm42view.py decodes it), channel 2 lines both ways, overflow, 4..10 no input,
 * the 255-byte cap, and no rows to a closed port. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dmcp.h"
#include "sercon_ifc.h"
#include "tport.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); \
                                       printf("\n"); } } while (0)

/* ---- a fake LCD line buffer: x -> byte 1 + x/8, most significant bit first, ink = 1 (not DMCP's layout) */
static uint8_t lb[LCD_Y][54];
uint8_t *lcd_line_addr(int y) { return lb[y]; }
static void set(int x, int y, int v) {
  if (v) lb[y][1 + x / 8] |= (uint8_t)(0x80 >> (x % 8)); else lb[y][1 + x / 8] &= (uint8_t)~(0x80 >> (x % 8));
}
static int get(int x, int y) { return lb[y][1 + x / 8] >> (7 - x % 8) & 1; }
void lcd_fill_rect(uint32_t x, uint32_t y, uint32_t dx, uint32_t dy, int val) {
  for (uint32_t j = y; j < y + dy; j++) for (uint32_t i = x; i < x + dx; i++) set((int)i, (int)j, val);
}
static uint32_t ms;
uint32_t sys_current_ms(void) { return ms += 1; }
int key_empty(void) { return 1; }
void reset_auto_off(void) {}
static int keys[16], nkeys;
int key_push(int k) { if (nkeys < 16) keys[nkeys++] = k; return 0; }

/* ---- a fake port */
static int up, open_, room = 4096;
static uint8_t rx[4096], tx[1 << 20];
static int nrx, ntx;
int tport_start(const char *p) { (void)p; up = 1; return 0; }
void tport_stop(void) { up = 0; }
int tport_mounted(void) { return up; }
int tport_connected(void) { return up && open_; }
void tport_task(void) {}
int tport_rx(uint8_t *p, int max) {
  int n = nrx < max ? nrx : max;
  memcpy(p, rx, (size_t)n); memmove(rx, rx + n, (size_t)(nrx - n)); nrx -= n;
  return n;
}
int tport_tx_room(void) { return up && open_ ? room : 0; }
int tport_tx(const void *p, int n) { memcpy(tx + ntx, p, (size_t)n); ntx += n; return n; }
void tport_wait(uint32_t w, void (*poll)(void)) { (void)w; poll(); }
const char *tport_product(void) { return "test"; }

static void host(const char *s, int n) { memcpy(rx + nrx, s, (size_t)n); nrx += n; usbcon_poll(); }
#define HOST(s) host(s, (int)sizeof(s) - 1)

/* the stream's lines as (channel, bytes): as tools/dm42view.py reads it */
static int nl, lch[4096], llen[4096];
static uint8_t lines[4096][300];
static void parse(void) {
  int ch = 0, n = 0;
  nl = 0;
  for (int i = 0; i < ntx; i++) {
    uint8_t b = tx[i];
    if (b >= 0x10 && b <= 0x1a) ch = b - 0x10;
    else if (b == '\n') { lch[nl] = ch; llen[nl] = n; nl++; n = 0; }
    else if (n < 300) lines[nl][n++] = b;
  }
}
/* an LCD row line -> the 400 pixels (1 = ink), as dm42view.py decodes it */
static int row_px(int i, int *yy, uint8_t *out) {
  if (lch[i] != 0 || llen[i] < 4 || lines[i][0] != 'L' || lines[i][1] != ' ') return 0;
  char h[3] = { (char)lines[i][2], (char)lines[i][3], 0 };
  *yy = (int)strtol(h, NULL, 16);
  uint8_t bits[58]; int nb = 0;
  for (int k = 4; k < llen[i]; k++) if (lines[i][k] & 0x80) bits[nb++] = lines[i][k] & 0x7F;
  if (nb != 58) return 0;
  uint8_t row[50] = { 0 };
  for (int b = 0; b < 400; b++) if (bits[b / 7] >> (b % 7) & 1) row[b / 8] |= (uint8_t)(1 << (b % 8));
  for (int x = 0; x < 400; x++) out[x] = !((row[49 - x / 8] >> (7 - x % 8)) & 1);
  return 1;
}

int main(void) {
  /* a picture: a diagonal, a box at the top left, the bottom row's first pixels */
  for (int i = 0; i < 240; i++) set(i + 80, i, 1);
  lcd_fill_rect(3, 5, 20, 7, 1);
  set(0, 239, 1); set(9, 239, 1);

  usbcon_ident("TEST47", "9.9");
  usb_serial_ioctl(SERIO_IS_ENABLED, 1);
  CHECK(usb_serial_ioctl(SERIO_IS_ENABLED, SER_IOCTL_READ) == 1, "enabled");
  CHECK(usbcon_layout_ok(), "layout learnt");
  CHECK(get(0, 239) && get(9, 239) && !get(1, 239) && !get(8, 239), "learn() put the pixels back");
  CHECK(usb_serial_ioctl(SERIO_IS_CONNECTED, SER_IOCTL_READ) == 0, "port closed");
  CHECK(usb_serial_ioctl(0x55, SER_IOCTL_READ) == SER_IOCTL_INVALID, "unknown op");

  /* rows while nobody listens: counted as sent, nothing out */
  usb_serial_ioctl(SERIO_SEND_LCD, 1);
  usbcon_lcd_refreshed(); usbcon_poll();
  CHECK(ntx == 0, "no bytes to a closed port (%d)", ntx);

  open_ = 1;
  HOST("\x10V\n");
  parse();
  CHECK(nl == 1 && lch[0] == 0 && !memcmp(lines[0], "V|HW TEST47|VER 9.9|DMCP5 TinyUSB|", 33), "V: %.*s", llen[0], lines[0]);

  /* L: key 77 then a release, every non-blank row on the next refresh, top row first, pixels right */
  ntx = 0; nkeys = 0;
  HOST("\x10L\n");
  CHECK(nkeys == 2 && keys[0] == 77 && keys[1] == 0, "L -> key 77, release (%d keys)", nkeys);
  usbcon_lcd_refreshed(); usbcon_poll();
  parse();
  int rows = 0, bad = 0, last = 999, order = 1;
  for (int i = 0; i < nl; i++) {
    int yy; uint8_t p[400];
    if (!row_px(i, &yy, p)) { bad++; continue; }
    rows++;
    if (yy >= last) order = 0;
    last = yy;
    if (llen[i] != 62) bad++;
    for (int x = 0; x < 400; x++) if (p[x] != get(x, 239 - yy)) { bad++; printf("row %02X differs\n", yy); break; }
  }
  int nonblank = 0;
  for (int y = 0; y < 240; y++) { int k = 0; for (int x = 0; x < 400; x++) k |= get(x, y); nonblank += k; }
  CHECK(rows == nonblank && !bad, "L: %d rows (want %d non-blank), %d bad", rows, nonblank, bad);
  CHECK(order, "rows top first");

  /* a change: only that row */
  ntx = 0;
  set(300, 100, 1);
  usbcon_lcd_refreshed(); usbcon_poll();
  parse();
  { int yy; uint8_t p[400];
    CHECK(nl == 1 && row_px(0, &yy, p) && yy == 239 - 100 && p[300] == 1, "one changed row (%d lines)", nl); }
  /* the FIFO full: the row waits for the next poll */
  ntx = 0; room = 10;
  set(7, 50, 1);
  usbcon_lcd_refreshed(); usbcon_poll();
  CHECK(ntx == 0, "nothing while the FIFO is full");
  room = 4096; usbcon_poll(); parse();
  CHECK(nl == 1, "the row goes out later (%d)", nl);

  /* keys */
  nkeys = 0;
  HOST("\x10KP13\n\x10KR\n\x10K 1c\n\x10KP1C12\n");
  CHECK(nkeys == 5 && keys[0] == 0x13 && keys[1] == 0 && keys[2] == 0x1c && keys[3] == 0 && keys[4] == 0x12,
        "keys %d: %x %x %x %x %x", nkeys, keys[0], keys[1], keys[2], keys[3], keys[4]);

  /* channel 2: the program's lines, both ways */
  HOST("\x12" "e2e4\n" "\x12" "go\n");
  CHECK(usb_serial_ioctl(SERIO_LINE_READY, 2) == 2, "two lines waiting");
  CHECK(usb_serial_ioctl(SERIO_LINE_LENGTH, 2) == 5, "length with its '\\n'");
  uint8_t buf[USIN_CHAN_SIZE + 1], ovf = 9;
  int n = usb_serial_read_line(2, buf, 3, &ovf);
  CHECK(n == -1, "too long for the buffer: -1");
  n = usb_serial_read_line(2, buf, USIN_CHAN_SIZE, &ovf);
  CHECK(n == 5 && !memcmp(buf, "e2e4\n", 5) && ovf == 0, "read e2e4");
  n = usb_serial_read_line(2, buf, USIN_CHAN_SIZE, &ovf);
  CHECK(n == 3 && !memcmp(buf, "go\n", 3), "read go");
  CHECK(usb_serial_read_line(2, buf, USIN_CHAN_SIZE, &ovf) == 0, "none left");
  ntx = 0;
  usb_serial_write(2, "bestmove e7e5\n", 14);
  parse();
  CHECK(nl == 1 && lch[0] == 2 && llen[0] == 13 && tx[0] == 0x12, "our line on channel 2");

  /* an overflowing line: ends in 0x0f, flagged; the next line is fine */
  char big[400];
  big[0] = 0x13;
  memset(big + 1, 'x', 300);
  big[301] = '\n';
  host(big, 302);
  n = usb_serial_read_line(3, buf, USIN_CHAN_SIZE, &ovf);
  CHECK(n == USIN_CHAN_SIZE && ovf == 1 && buf[n - 1] == 0x0f, "overflow: n %d ovf %d", n, ovf);
  HOST("\x13ok\n");
  n = usb_serial_read_line(3, buf, USIN_CHAN_SIZE, &ovf);
  CHECK(n == 3 && ovf == 0 && !memcmp(buf, "ok\n", 3), "after the overflow");

  /* 16 lines at most */
  for (int i = 0; i < 20; i++) HOST("\x12" "a\n");
  CHECK(usb_serial_ioctl(SERIO_LINE_READY, 2) == 16, "16 lines kept");
  while (usb_serial_read_line(2, NULL, USIN_CHAN_SIZE, NULL)) {}

  /* channels 4..10: no input */
  HOST("\x15zzz\n");
  CHECK(usb_serial_ioctl(SERIO_LINE_READY, 5) == 0, "no input on 5");

  /* 255 bytes a call at most */
  char w[400];
  memset(w, 'y', sizeof w);
  ntx = 0;
  usb_serial_write(4, w, 400);
  CHECK(ntx == 256 && tx[0] == 0x14, "255 bytes + the id (%d)", ntx);

  usb_serial_ioctl(SERIO_IS_ENABLED, 0);
  CHECK(usb_serial_ioctl(SERIO_IS_ENABLED, SER_IOCTL_READ) == 0, "off");
  CHECK(usb_serial_read_line(2, buf, USIN_CHAN_SIZE, &ovf) == 0, "off: no lines");

  printf(fails ? "usbcon: %d FAILED\n" : "usbcon: all checks passed\n", fails);
  return fails != 0;
}
