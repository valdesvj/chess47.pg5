/* usbview.h - Chess 47: the screen to the PC over USB, for tools/dm42view.py: src/sercon.c, DMCP5's serial console in
 * both builds (make: DMCP5 3.58's own; make USB=tinyusb: played by src/usb/usbcon.c over our own TinyUSB port).
 * GPL-3.0-or-later. */
#ifndef CHESS47_USBVIEW_H
#define CHESS47_USBVIEW_H

void usbview_on(void);       /* the port is already on (usbser_start) */
void usbview_off(void);
int  usbview_is_on(void);
void usbview_send(void);     /* after each redraw (the TinyUSB build sends the changed rows then) */
void usbview_redraw_request(void);   /* DMCP's key 77 (serial console: the PC asked for the whole screen) */
#define USBVIEW_KEY_REDRAW 77

#endif
