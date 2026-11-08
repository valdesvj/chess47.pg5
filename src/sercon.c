/* sercon.c - Chess 47 over DMCP5 3.58's own serial console (IFC 3.18): the same interface as src/usb/usbser.c
 * (TinyUSB) and src/usbview.c, so main.c and the game protocol do not change. No TinyUSB.
 *
 *   usbser_start   the console on, and DMCP's LCD stream (the screen: DMCP sends every changed row on channel 0);
 *                  the user's DMCP settings are kept and given back by usbser_stop
 *   usbser_puts    our text (moves, newgame, result ...) on channel 2, whole lines only, at most 128 bytes a call
 *                  (the data sheet: longer or quick calls lose their ends); short lines joined up to that
 *   usbser_read    the PC's lines from channel 2 (usb_serial_read_line), as bytes with their '\n'
 *   usbser_idle    sleep with a 100 ms timer while the cable is in: DMCP buffers only 256 bytes / 16 lines a
 *                  channel, and a line from the PC may not wake the calculator
 *   usbview_*      nothing to send: DMCP streams the LCD; key 77 (the PC sent L) -> redraw + lcd_forced_refresh
 *
 * The PC adds 0x12 (channel 2) before each line it sends, and reads our lines on channel 2 (tools/dm42port.py).
 * Copyright (C) 2026 Victor Valdes - MIT. */
#include <main.h>
#include <dmcp.h>
#include <string.h>
#include "usbser.h"
#include "usbview.h"
#include "sercon_ifc.h"

#define CH      2                         /* our channel, both ways */
#define CAP     128                       /* bytes a usb_serial_write */
#define RD      SER_IOCTL_READ

static int on, was_en = -1, was_lcd = -1;
static char out[1024];                    /* text waiting: whole lines go out at flush */
static int nout;
static uint8_t in[USIN_CHAN_SIZE + 1];    /* the line being handed to usbser_read */
static int nin, atin;

static int sw(int op) { return usb_serial_ioctl(op, RD); }
static void put_back(int op, int v) { if (v == 0 || v == 1) usb_serial_ioctl(op, v); }

int usbser_start(const char *product) {
  (void)product;
  if (on) return 0;
  was_en = sw(SERIO_IS_ENABLED);
  was_lcd = sw(SERIO_SEND_LCD);
  usb_serial_ioctl(SERIO_IS_ENABLED, 1);
  usb_serial_ioctl(SERIO_SEND_LCD, 1);
  on = 1; nout = nin = atin = 0;
  for (int i = 0; i < USIN_MAX_READLINES && usb_serial_ioctl(SERIO_LINE_READY, CH) > 0; i++)
    usb_serial_read_line(CH, NULL, USIN_CHAN_SIZE, NULL);   /* lines from before: not for this session */
  return 0;
}

void usbser_stop(void) {
  if (!on) return;
  put_back(SERIO_SEND_LCD, was_lcd);
  put_back(SERIO_IS_ENABLED, was_en);
  on = 0;
}

int usbser_active(void) { return on && sw(SERIO_IS_ACTIVE) > 0; }
int usbser_connected(void) { return usbser_active() && sw(SERIO_IS_CONNECTED) > 0; }
int usbser_mounted(void) { return usbser_active(); }

/* the whole lines waiting, joined into calls of at most CAP bytes (a longer line is cut: none is) */
static void flush(void) {
  if (!on) return;
  if (!usbser_connected()) { nout = 0; return; }      /* nobody listening: drop, as the TinyUSB port did */
  int done = 0;
  for (;;) {
    char *nl = memchr(out + done, '\n', (size_t)(nout - done));
    if (!nl) break;                                    /* the rest is not a whole line yet */
    int n = (int)(nl - (out + done)) + 1;
    if (n > CAP) { out[done + CAP - 1] = '\n'; usb_serial_write(CH, out + done, CAP); done += n; continue; }
    for (;;) {                                         /* join the next whole lines while they fit */
      char *q = memchr(out + done + n, '\n', (size_t)(nout - done - n));
      if (!q) break;
      int l = (int)(q - (out + done + n)) + 1;
      if (n + l > CAP) break;
      n += l;
    }
    usb_serial_write(CH, out + done, n);
    done += n;
  }
  memmove(out, out + done, (size_t)(nout - done));
  nout -= done;
}

int usbser_write(const void *p, int n) {
  if (!on) return -1;
  const char *c = (const char *)p;
  for (int i = 0; i < n; i++) {
    if (c[i] == '\r') continue;                        /* lines end in '\n' on the console */
    if (c[i] == '\n' && (nout == 0 || out[nout - 1] == '\n')) continue;   /* no empty lines */
    if (nout >= (int)sizeof out) flush();
    if (nout >= (int)sizeof out) nout = 0;             /* the PC does not read: start again */
    out[nout++] = c[i];
  }
  return n;
}
int usbser_puts(const char *s) { return usbser_write(s, (int)strlen(s)); }

void usbser_task(void) { flush(); }

int usbser_read(void *p, int max) {
  flush();
  if (atin >= nin) {
    nin = atin = 0;
    if (!usbser_connected() || usb_serial_ioctl(SERIO_LINE_READY, CH) <= 0) return 0;
    uint8_t ovf = 0;
    int n = usb_serial_read_line(CH, in, USIN_CHAN_SIZE, &ovf);
    if (n < 0) { usb_serial_read_line(CH, NULL, USIN_CHAN_SIZE, NULL); return 0; }   /* too long: discard */
    if (n == 0 || ovf || in[n - 1] != '\n') return 0;                                  /* cut short: drop */
    nin = n;
  }
  int k = nin - atin < max ? nin - atin : max;
  memcpy(p, in + atin, (size_t)k);
  atin += k;
  return k;
}

void usbser_idle(uint32_t ms) {
  flush();
  sys_timer_start(0, usbser_active() && ms > 100 ? 100 : ms);   /* poll the PC's lines every 100 ms */
  CLR_ST(STAT_RUNNING);
  sys_sleep();
}

const char *usbser_product(void) { return "DMCP serial console"; }

/* the screen: DMCP streams it */
void usbview_on(void) {}
void usbview_off(void) {}
int usbview_is_on(void) { return on; }
void usbview_send(void) {}
void usbview_redraw_request(void) { lcd_forced_refresh(); }
