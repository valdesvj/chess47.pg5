/* usbser_sim.c - the PC simulator's stand-in for src/usb/usbser.c: the port is always "open"; what is sent goes to
 * ./acm.out (tools/dm42view.py acm.out shows it); what the "PC" sends comes from ./acm.in, one line each time the
 * calculator has sent a move (a PC answering it: the vs USB mode). */
#include <stdio.h>
#include <string.h>
#include "usbser.h"

static FILE *f;
static int answer, moves;              /* the human has just moved (1st, 3rd ... move sent): the "PC" may answer */
int usbser_start(const char *product) { (void)product; if (!f) f = fopen("acm.out", "w"); return f ? 0 : -2; }
void usbser_stop(void) { if (f) fclose(f); f = NULL; }
int usbser_active(void) { return f != NULL; }
int usbser_connected(void) { return f != NULL; }
int usbser_mounted(void) { return f != NULL; }
void usbser_task(void) {}
int usbser_write(const void *p, int n) {
  if (!f) return -1;
  const char *c = (const char *)p;
  if (n >= 6 && c[0] >= 'a' && c[0] <= 'h' && c[1] >= '1' && c[1] <= '8' && c[2] >= 'a' && c[2] <= 'h') answer = (++moves % 2 == 1);
  fwrite(p, 1, (size_t)n, f);
  fflush(f);
  return n;
}
int usbser_puts(const char *s) { return usbser_write(s, (int)strlen(s)); }
int usbser_read(void *p, int max) {
  static FILE *in;
  static int done;
  char line[128];
  if (!f || done || !answer) return 0;
  if (!in && !(in = fopen("acm.in", "r"))) { done = 1; return 0; }
  if (!fgets(line, sizeof line, in)) { done = 1; return 0; }
  answer = 0;
  line[strcspn(line, "\r\n")] = 0;
  int k = snprintf((char *)p, (size_t)max, "%s\r\n", line);
  return k < max ? k : max;
}
void sys_sleep(void);
void usbser_idle(uint32_t ms) { (void)ms; sys_sleep(); }   /* the sim: the next scripted key (or the end) */
const char *usbser_product(void) { return "sim"; }
