/* usbview.c - Chess 47: sends the LCD to the PC over USB (our own serial port, src/usb/usbser.c), one text frame per screen:
 *   CHESS47 LCD 400 240\n   240 lines of 100 hex digits (x = 0 first, most significant bit first, 1 = ink)   END\n
 * The DMCP line buffer's layout (byte order, bit order, inverted data) is learnt at start by setting one pixel and
 * looking which bit changes, so it does not depend on undocumented details. tools/dm42view.py shows the frames (the same viewer as NAV47).
 * Copyright (C) 2026 Victor Valdes - MIT (as Chess 47). */
#include <main.h>
#include <dmcp.h>
#include <string.h>
#include "usbview.h"
#include "usbser.h"

#define SCAN 54                       /* look at the whole line buffer (command and dummy bytes too) */
static int on, ok;
static int b0, bstep, k0, kstep, ink;   /* x -> byte b0 + bstep*(x/8), bit k0 + kstep*(x%8); ink = the bit value */

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
  ok = find(0, y, &bx, &kx, &v) && find(1, y, &b1, &k1, &v1) && find(8, y, &b8, &k8, &v8) && b1 == bx;
  if (!ok) return;
  b0 = bx; k0 = kx; ink = v;
  kstep = k1 - kx;
  bstep = b8 - bx;
  ok = (kstep == 1 || kstep == -1) && (bstep == 1 || bstep == -1);
}

void usbview_redraw_request(void) {}          /* the serial console's key 77: not in the TinyUSB build */
void usbview_on(void) { on = 1; learn(); }       /* main.c switches the port on (usbser_start) */
void usbview_off(void) { on = 0; }
int usbview_is_on(void) { return on; }

void usbview_send(void) {
  if (!on || !ok || !usbser_connected()) return;
  static const char HEX[] = "0123456789ABCDEF";
  char line[LCD_X / 4 + 2];
  usbser_puts("CHESS47 LCD 400 240\n");
  for (int y = 0; y < LCD_Y; y++) {
    const uint8_t *p = lcd_line_addr(y);
    for (int c = 0; c < LCD_X / 8; c++) {
      int byte = 0;
      const uint8_t src = p[b0 + bstep * c];
      for (int i = 0; i < 8; i++) {
        int bit = (src >> (k0 + kstep * i)) & 1;
        byte = (byte << 1) | (bit == ink);
      }
      line[2 * c] = HEX[byte >> 4];
      line[2 * c + 1] = HEX[byte & 15];
    }
    line[LCD_X / 4] = '\n';
    line[LCD_X / 4 + 1] = 0;
    usbser_puts(line);
  }
  usbser_puts("END\n");
}
