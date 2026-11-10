/* tport.c - the DM42n's USB serial port driven by the program (TinyUSB 0.21, DWC2 OTG_FS of the STM32U575), for
 * src/usb/usbcon.c (make USB=tinyusb: DMCP5 3.58's serial console played over our own port).
 *
 * The firmware keeps the USB core but leaves it soft-disconnected; we take it for as long as the port is on:
 *  - the USB supply (PWR SVMCR.USV), the 48 MHz clock (HSI48 trimmed by the CRS on the USB SOF), the core's clock;
 *  - the interrupt: a copy of the vector table in RAM with our handler for OTG_FS (IRQ 73), VTOR pointed at it;
 *    the firmware's own table is put back by tport_stop();
 *  - TinyUSB's device stack: one CDC port.
 * While the port is on the CPU must not stop (sys_sleep would stop the USB): tport_wait() runs the USB instead.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <stdlib.h>
#include <string.h>
#include "stm32u5xx.h"
#include "tusb.h"
#include "tport.h"

#define REG(a) (*(volatile uint32_t *)(a))
#define RCC          0x46020C00UL
#define RCC_CR       REG(RCC + 0x00)
#define RCC_AHB2ENR1 REG(RCC + 0x8C)
#define RCC_AHB3ENR  REG(RCC + 0x94)
#define RCC_APB1ENR1 REG(RCC + 0x9C)
#define RCC_CCIPR1   REG(RCC + 0xE0)
#define PWR_SVMCR    REG(0x46020800UL + 0x10)
#define CRS_CR       REG(0x40006000UL + 0x00)
#define CRS_CFGR     REG(0x40006000UL + 0x04)
#define SCB_VTOR     REG(0xE000ED08UL)
#define NVEC         (16 + 160)                   /* the U575's 141 interrupts and room to spare */

uint32_t SystemCoreClock = 160000000u;            /* DMCP runs at 160 MHz on USB power (dwc2: turnaround time) */
uint32_t tusb_time_millis_api(void) { return sys_current_ms(); }
void tusb_time_delay_ms_api(uint32_t ms) { uint32_t t = sys_current_ms(); while (sys_current_ms() - t < ms) {} }

static int on;
static void *vt_mem;
static uint32_t old_vtor;
static const char *prod = "47 calculator";

const char *tport_product(void) { return prod; }
static void otg_irq(void) { tud_int_handler(0); }

static void clocks(void) {
  RCC_AHB3ENR |= 1u << 2;                         /* PWR clock */
  (void)RCC_AHB3ENR;
  PWR_SVMCR |= 1u << 28;                          /* USV: VDDUSB valid, the USB transceiver out of isolation */
  RCC_CR |= 1u << 12;                             /* HSI48 on */
  for (uint32_t t = sys_current_ms(); !(RCC_CR & (1u << 13)) && sys_current_ms() - t < 50;) {}
  RCC_CCIPR1 &= ~(3u << 26);                      /* ICLKSEL = HSI48 for the USB */
  RCC_APB1ENR1 |= 1u << 24;                       /* CRS clock */
  (void)RCC_APB1ENR1;
  CRS_CFGR = (CRS_CFGR & ~(3u << 28)) | (2u << 28);   /* sync on the USB start of frame */
  CRS_CR |= (1u << 6) | (1u << 5);                /* AUTOTRIMEN, CEN */
  RCC_AHB2ENR1 |= 1u << 14;                       /* OTG_FS clock */
  (void)RCC_AHB2ENR1;
}

int tport_start(const char *product) {
  if (on) return 0;
  if (!usb_powered()) return -1;
  if (product) prod = product;
  vt_mem = malloc(NVEC * 4 + 1024);
  if (!vt_mem) return -2;
  if (usb_is_on()) usb_turn_off();                /* the firmware's own serial port, if it was asked for */
  NVIC_DisableIRQ(OTG_FS_IRQn);
  clocks();
  uint32_t *vt = (uint32_t *)(((uintptr_t)vt_mem + 1023u) & ~(uintptr_t)1023u);
  old_vtor = SCB_VTOR;
  memcpy(vt, (const void *)old_vtor, NVEC * 4);
  vt[16 + OTG_FS_IRQn] = (uint32_t)(uintptr_t)otg_irq;
  __DSB();
  SCB_VTOR = (uint32_t)(uintptr_t)vt;
  __DSB();
  __ISB();
  tusb_rhport_init_t init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
  tusb_rhport_init(0, &init);                     /* resets the core, connects (DCTL.SDIS = 0), enables the IRQ */
  on = 1;
  return 0;
}

void tport_stop(void) {
  if (!on) return;
  tud_disconnect();
  tusb_deinit(0);
  NVIC_DisableIRQ(OTG_FS_IRQn);
  REG(0x42040000UL + 0x804) |= 2u;                /* DCTL.SDIS: disconnected, as the firmware had it */
  REG(0x42040000UL + 0x008) &= ~1u;               /* GAHBCFG.GINTMSK off */
  __DSB();
  SCB_VTOR = old_vtor;
  __DSB();
  __ISB();
  free(vt_mem);
  vt_mem = NULL;
  on = 0;
}

int tport_mounted(void) { return on && tud_mounted(); }
int tport_connected(void) { return on && tud_cdc_connected(); }
void tport_task(void) { if (on) tud_task(); }

int tport_rx(uint8_t *p, int max) {
  if (!on || !tud_cdc_available()) return 0;
  return (int)tud_cdc_read(p, (uint32_t)max);
}

int tport_tx_room(void) { return on && tud_cdc_connected() ? (int)tud_cdc_write_available() : 0; }

int tport_tx(const void *p, int n) {
  if (!on) return 0;
  int k = (int)tud_cdc_write(p, (uint32_t)n);
  tud_cdc_write_flush();
  return k;
}

void tport_wait(uint32_t ms, void (*poll)(void)) {
  uint32_t t = sys_current_ms(), r = t;
  do {
    poll();
    if (sys_current_ms() - r > 10000) { reset_auto_off(); r = sys_current_ms(); }   /* the USB keeps it on */
  } while (sys_current_ms() - t < ms && key_empty());
}
