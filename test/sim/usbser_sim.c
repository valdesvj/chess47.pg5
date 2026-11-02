/* usbser_sim.c - the PC simulator's stand-in for src/usb/usbser.c: the port is always "open", what is sent goes
 * to ./acm.out (tools/dm42view.py acm.out shows it). */
#include <stdio.h>
#include <string.h>
#include "usbser.h"

static FILE *f;
int usbser_start(const char *product) { (void)product; if (!f) f = fopen("acm.out", "w"); return f ? 0 : -2; }
void usbser_stop(void) { if (f) fclose(f); f = NULL; }
int usbser_active(void) { return f != NULL; }
int usbser_connected(void) { return f != NULL; }
int usbser_mounted(void) { return f != NULL; }
void usbser_task(void) {}
int usbser_write(const void *p, int n) { if (!f) return -1; fwrite(p, 1, (size_t)n, f); fflush(f); return n; }
int usbser_puts(const char *s) { return usbser_write(s, (int)strlen(s)); }
int usbser_read(void *p, int max) { (void)p; (void)max; return 0; }
void sys_sleep(void);
void usbser_idle(uint32_t ms) { (void)ms; sys_sleep(); }   /* the sim: the next scripted key (or the end) */
const char *usbser_product(void) { return "sim"; }
