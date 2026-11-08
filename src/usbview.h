/* usbview.h - Chess 47: the screen to the PC over USB, for tools/dm42view.py. Two builds of the same interface:
 * src/sercon.c (make, USB=sercon: DMCP5 3.58's serial console streams the LCD) and src/usbview.c (make USB=tinyusb).
 * MIT. */
#ifndef CHESS47_USBVIEW_H
#define CHESS47_USBVIEW_H

void usbview_on(void);       /* learns the LCD buffer's layout (the port is already on) */
void usbview_off(void);
int  usbview_is_on(void);
void usbview_send(void);     /* the whole LCD (as shown, menu bar too) as one text frame */
void usbview_redraw_request(void);   /* DMCP's key 77 (serial console: the PC asked for the whole screen) */
#define USBVIEW_KEY_REDRAW 77

#endif
