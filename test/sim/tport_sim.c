/* tport_sim.c - the PC simulator's stand-in for src/usb/tport.c (make sim USB=tinyusb): the port is always open; what
 * src/usb/usbcon.c sends goes to ./acm.out. The "PC" answers from ./acm.in on channel 2, one line after each move the
 * calculator sends (1st, 3rd ...: a PC answering the human), or every time with ACM_FREE set, as
 * test/sim/sercon_sim.c does. HOSTL in the key script is the PC sending L on channel 0.
 * So the simulator checks usbcon.c's stream against sercon_sim.c's (DMCP's): tools/dm42view.py acm.out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "tport.h"
#include "sercon_ifc.h"

static FILE *f, *in;
static uint8_t rx[512];
static int nrx, ch, answer, moves, hello, done;
static char cur[8];                     /* the start of the channel-2 line being sent */
static int ncur;

int tport_start(const char *product) { (void)product; if (!f) f = fopen("acm.out", "wb"); return f ? 0 : -2; }
void tport_stop(void) { if (f) fclose(f); f = NULL; }
int tport_mounted(void) { return f != NULL; }
int tport_connected(void) { return f != NULL; }
void tport_task(void) {}

int tport_rx(uint8_t *p, int max) {
  /* the PC's next line, once the program has spoken on channel 2 and has read the last one */
  if (!nrx && f && hello && !done && (answer || getenv("ACM_FREE")) && usb_serial_ioctl(SERIO_LINE_READY, 2) <= 0) {
    char line[USIN_CHAN_SIZE];
    if (!in && !(in = fopen("acm.in", "r"))) done = 1;
    else if (!fgets(line, (int)sizeof line - 2, in)) done = 1;
    else {
      answer = 0;
      line[strcspn(line, "\r\n")] = 0;
      rx[nrx++] = 0x12;
      int n = (int)strlen(line);
      memcpy(rx + nrx, line, (size_t)n); nrx += n;
      rx[nrx++] = '\n';
    }
  }
  int n = nrx < max ? nrx : max;
  memcpy(p, rx, (size_t)n);
  memmove(rx, rx + n, (size_t)(nrx - n));
  nrx -= n;
  return n;
}

int tport_tx_room(void) { return f ? 1024 : 0; }

int tport_tx(const void *p, int n) {
  if (!f) return 0;
  const uint8_t *b = (const uint8_t *)p;
  for (int i = 0; i < n; i++) {                       /* watch channel 2 for moves (e2e4, e7e8q): the PC may answer */
    if (b[i] >= 0x10 && b[i] <= 0x1a) { ch = b[i] - 0x10; ncur = 0; continue; }
    if (ch != 2) continue;
    hello = 1;
    if (b[i] == '\n') {
      if (ncur >= 4 && cur[0] >= 'a' && cur[0] <= 'h' && cur[1] >= '1' && cur[1] <= '8' && cur[2] >= 'a' &&
          cur[2] <= 'h') answer = (++moves % 2 == 1);
      ncur = 0;
    } else if (ncur < (int)sizeof cur) cur[ncur++] = (char)b[i];
  }
  fwrite(p, 1, (size_t)n, f);
  fflush(f);
  return n;
}

void sys_sleep(void);
void tport_wait(uint32_t ms, void (*poll)(void)) { (void)ms; poll(); sys_sleep(); }   /* the next scripted key */
const char *tport_product(void) { return "sim"; }

/* the PC sends L (the sim delivers key 77 itself) */
void sim_host_L(void) { static const char L[] = "\x10L\n"; memcpy(rx + nrx, L, 3); nrx += 3; }
int key_push(int k1) { (void)k1; return 0; }
