/* usbser.h - Chess 47: the USB serial port for the game protocol (moves, newgame, ...) and the main loop, both builds:
 * src/sercon.c, on DMCP5's serial console, channel 2 (make: DMCP5 3.58's own; make USB=tinyusb: played over our own
 * TinyUSB port by src/usb/usbcon.c). GPL-3.0-or-later. */
#ifndef CHESS47_USBSER_H
#define CHESS47_USBSER_H
#include <stdint.h>

int  usbser_start(const char *product);   /* 0 ok, -1 no USB power, -2 no memory */
void usbser_stop(void);
int  usbser_active(void);
int  usbser_connected(void);              /* the PC has the port open (DTR) */
int  usbser_mounted(void);                /* the PC has enumerated the device */
void usbser_task(void);                   /* call often (a long search): our lines out, the USB runs */
int  usbser_write(const void *p, int n);  /* whole lines go out on channel 2 (at most 128 bytes a call) */
int  usbser_puts(const char *s);
int  usbser_read(void *p, int max);       /* what the PC sent: up to max bytes, 0 if none */
/* instead of sys_sleep() while the port is on: runs the USB until a key, or ms milliseconds */
void usbser_idle(uint32_t ms);
const char *usbser_product(void);
#endif
