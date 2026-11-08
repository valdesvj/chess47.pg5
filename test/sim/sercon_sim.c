/* sercon_sim.c - the PC simulator's DMCP5 3.58 serial console (SwissMicros' data sheet), into ./acm.out:
 *   channel 0: with SERIO_SEND_LCD on, each refresh sends the rows that differ from the ones last sent:
 *              0x10 "L <yy>" + the row's 50 bytes packed 7 bits a byte (LSB first, top bit set) + '\n'
 *              (yy hex, 239 = the top row; pixel x in byte 49 - x/8, bit 7 - x%8, dark = 0: lcd_line_addr's bytes)
 *   channel 2: Chess 47's lines, each usb_serial_write with the id byte 0x12 first
 * The "PC" answers from ./acm.in on channel 2, one line after each move the calculator sends (1st, 3rd ...: a PC
 * answering the human), or every time with ACM_FREE set, as test/sim/usbser_sim.c. HOSTL in the key script: every
 * row unsent and key 77 for the program. tools/dm42view.py acm.out shows the screen; tools/dm42port.py reads the lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dmcp.h"
#include "sercon_ifc.h"

static int en, lcd, sent_ok[LCD_Y], answer, moves;
static uint8_t sent[LCD_Y][50];
static FILE *f, *in;
static char rx[USIN_CHAN_SIZE + 1];
static int rxn = -1, rxdone, hello;   /* the PC answers once the program has said something on channel 2 */

static void out(int ch, const char *b, int n) {
  if (!f) f = fopen("acm.out", "wb");
  fputc(US_CHAN0_CHAR + ch, f); fwrite(b, 1, (size_t)n, f); fflush(f);
}

int usb_serial_ioctl(int op, int val) {
  int rd = val == SER_IOCTL_READ;
  switch (op) {
  case SERIO_IS_ENABLED: if (!rd) en = val ? 1 : 0; return en;
  case SERIO_SEND_LCD: if (!rd) lcd = val ? 1 : 0; return lcd;
  case SERIO_IS_ACTIVE: case SERIO_IS_CONNECTED: return en;
  case SERIO_LINE_READY: case SERIO_LINE_LENGTH: {
    if (val != 2 || !en || !hello) return 0;
    if (rxn < 0 && !rxdone && (answer || getenv("ACM_FREE"))) {     /* the PC's next line */
      if (!in && !(in = fopen("acm.in", "r"))) { rxdone = 1; return 0; }
      if (!fgets(rx, USIN_CHAN_SIZE - 1, in)) { rxdone = 1; return 0; }
      answer = 0;
      rx[strcspn(rx, "\r\n")] = 0; strcat(rx, "\n");
      rxn = (int)strlen(rx);
    }
    return rxn < 0 ? 0 : op == SERIO_LINE_READY ? 1 : rxn;
  }
  }
  return rd ? 0 : (int)SER_IOCTL_INVALID;
}

void usb_serial_write(uint8_t chan_nr, const char *buf, int len) {
  if (!en) return;
  if (len > 255) len = 255;                       /* DMCP sends at most 255 bytes of a call */
  if (chan_nr == 2) hello = 1;
  if (chan_nr == 2)                               /* a move line (e2e4, e7e8q): the "PC" may answer */
    for (int i = 0; i < len; i = (int)((const char *)memchr(buf + i, '\n', (size_t)(len - i)) - buf) + 1) {
      const char *c = buf + i;
      if (len - i >= 5 && c[0] >= 'a' && c[0] <= 'h' && c[1] >= '1' && c[1] <= '8' && c[2] >= 'a' && c[2] <= 'h')
        answer = (++moves % 2 == 1);
      if (!memchr(buf + i, '\n', (size_t)(len - i))) break;
    }
  out(chan_nr, buf, len);
}

int usb_serial_read_line(uint8_t chan_nr, uint8_t *buf, int buflen, uint8_t *is_overflow) {
  if (is_overflow) *is_overflow = 0;
  if (chan_nr != 2 || usb_serial_ioctl(SERIO_LINE_READY, 2) <= 0) return 0;
  if (!buf) { rxn = -1; return 0; }               /* discard */
  if (rxn > buflen) return -1;
  int n = rxn;
  memcpy(buf, rx, (size_t)n); buf[n] = 0;
  rxn = -1;
  return n;
}

void sim_host_L(void) { memset(sent_ok, 0, sizeof sent_ok); }

void sim_lcd_out(int forced) {
  (void)forced;
  if (!en || !lcd) return;
  for (int y = 0; y < LCD_Y; y++) {
    const uint8_t *d = lcd_line_addr(y) + 2;
    int blank = 1;
    for (int i = 0; i < 50; i++) if (d[i] != 0xFF) blank = 0;
    if (sent_ok[y] && !memcmp(sent[y], d, 50)) continue;
    int was = sent_ok[y];
    memcpy(sent[y], d, 50); sent_ok[y] = 1;
    if (!was && blank) continue;                  /* after L: the non-blank rows */
    char line[4 + 58 + 1];
    int n = sprintf(line, "L %02X", LCD_Y - 1 - y);
    for (int j = 0; j < 58; j++) {
      int v = 0;
      for (int k = 0; k < 7; k++) { int i = j * 7 + k; if (i < 400 && (d[i / 8] >> (i % 8) & 1)) v |= 1 << k; }
      line[n++] = (char)(0x80 | v);
    }
    line[n++] = '\n';
    out(0, line, n);
  }
}
