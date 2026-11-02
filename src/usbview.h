/* usbview.h - Chess 47: the screen to the PC over the USB serial port (acm_puts), for tools/dm42view.py.
 * MIT. */
#ifndef CHESS47_USBVIEW_H
#define CHESS47_USBVIEW_H

void usbview_on(void);       /* learns the LCD buffer's layout (the port is already on) */
void usbview_off(void);
int  usbview_is_on(void);
void usbview_send(void);     /* the whole LCD (as shown, menu bar too) as one text frame */

#endif
