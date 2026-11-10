/* tport.h - the DM42n's USB as a serial port (/dev/ttyACM0), driven by the program itself with TinyUSB: the DMCP
 * firmware's usb_acm_on() leaves the port disconnected (USB47 step 1: DCTL.SDIS stays 1).
 * Only src/usb/usbcon.c uses it (make USB=tinyusb): usbcon.c plays DMCP5 3.58's serial console over this port.
 * test/sim/tport_sim.c is the PC simulator's stand-in.
 * GPL-3.0-or-later. */
#ifndef USB47_TPORT_H
#define USB47_TPORT_H
#include <stdint.h>

int  tport_start(const char *product);    /* 0 ok, -1 no USB power, -2 no memory */
void tport_stop(void);
int  tport_mounted(void);                 /* the PC has enumerated the device */
int  tport_connected(void);               /* the PC has the port open (DTR) */
void tport_task(void);                    /* TinyUSB's device task: call often */
int  tport_rx(uint8_t *p, int max);       /* what the PC sent: up to max bytes, 0 if none */
int  tport_tx_room(void);                 /* bytes that fit in the send FIFO now */
int  tport_tx(const void *p, int n);      /* into the send FIFO (n <= tport_tx_room()), and flush */
/* wait up to ms milliseconds or until a key, calling poll() all the time (the CPU must not stop: sys_sleep()
   would stop the USB) */
void tport_wait(uint32_t ms, void (*poll)(void));
const char *tport_product(void);
#endif
