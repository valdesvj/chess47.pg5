######################################
# CHESS 47 - DMCP5 program for SwissMicros DM42n
# Based on the SwissMicros DMCP5_SDK Makefile (BSD-3)
######################################
TARGET = CHESS47
# make                 the USB through DMCP5 3.58's serial console (needs DMCP5 3.58: the .pg5 has IFC 18)
# make both            both: build/CHESS47.pg5 and build/tinyusb/CHESS47.pg5
# make USB=tinyusb     the same serial console over our own TinyUSB port (any DMCP5): the same stream, the same tools
# make sim             the PC simulator (build/sim); make sim USB=tinyusb for the TinyUSB stand-in

ifdef DEBUG
DEBUG = 1
endif

BUILD_DIR = build
PYTHON ?= python3

######################################
# Sources
######################################
C_INCLUDES += -Idmcp -Isrc -Isrc/usb -Ithird_party/tinyusb/src
C_SOURCES  += dmcp/sys/pgm_syscalls.c
ASM_SOURCES = dmcp/startup_pgm.s

USB ?= sercon
C_SOURCES += src/main.c src/engine.c
ifeq ($(USB),sercon)
# DMCP5 3.58's serial console (IFC 18): the moves on channel 2, the screen streamed by DMCP (no TinyUSB)
C_SOURCES += src/sercon.c
C_DEFS += -DPLATFORM_IFC_VER=18
else
# DMCP5's serial console played over our own TinyUSB port (src/usb/usbcon.c; the firmware's usb_acm_on leaves the
# port disconnected): the same src/sercon.c, the same stream
TUSB = third_party/tinyusb/src
C_DEFS += -DSERCON_TINYUSB
C_SOURCES += src/sercon.c src/usb/usbcon.c src/usb/tport.c src/usb/usb_desc.c \
             $(TUSB)/tusb.c $(TUSB)/common/tusb_fifo.c $(TUSB)/device/usbd.c $(TUSB)/class/cdc/cdc_device.c \
             $(TUSB)/portable/synopsys/dwc2/dcd_dwc2.c $(TUSB)/portable/synopsys/dwc2/dwc2_common.c
endif

#######################################
# Toolchain
#######################################
CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

C_DEFS  += -D__weak="__attribute__((weak))" -D__packed="__attribute__((__packed__))"
CPUFLAGS += -mthumb -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16

CFLAGS  = -std=gnu11 $(CPUFLAGS) $(C_DEFS) $(C_INCLUDES) -Wall -fdata-sections -ffunction-sections
CFLAGS += -Wno-misleading-indentation
ifeq ($(DEBUG), 1)
CFLAGS += -O0 -g -DDEBUG
else
CFLAGS += -O2 -g
endif
CFLAGS += -MD -MP -MF .dep/$(@F).d

LDSCRIPT = stm32_program.ld
LDFLAGS  = $(CPUFLAGS) -T$(LDSCRIPT) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections \
           -Wl,--wrap=_malloc_r -g

all: $(BUILD_DIR)/$(TARGET).pg5

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

# QSPI CRC is compiled into the program header: if it changes, rebuild once.
$(BUILD_DIR)/$(TARGET).pg5: $(BUILD_DIR)/$(TARGET).elf tools/pg5tool.py
	$(OBJCOPY) --remove-section .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_flash.bin
	$(OBJCOPY) --only-section   .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_qspi.bin
	@if ! $(PYTHON) tools/pg5tool.py qspi $(BUILD_DIR)/$(TARGET)_qspi.bin src/qspi_crc.h; then \
	   rm -f $(BUILD_DIR)/pgm_syscalls.o $<; $(MAKE) --no-print-directory $@; exit $$?; fi
	$(PYTHON) tools/pg5tool.py pg5 $(BUILD_DIR)/$(TARGET)_flash.bin $@
	$(SIZE) $<

$(BUILD_DIR):
	mkdir -p $@

# PC test: perft + search
test: test/perft.c src/engine.c src/engine.h
	gcc -std=gnu11 -O2 -Wall -o $(BUILD_DIR)/perft test/perft.c src/engine.c && $(BUILD_DIR)/perft

# both .pg5 at once: build/<NAME>.pg5 (DMCP5 3.58's console) and build/tinyusb/<NAME>.pg5 (any DMCP5)
both:
	$(MAKE) USB=sercon
	$(MAKE) USB=tinyusb BUILD_DIR=$(BUILD_DIR)/tinyusb

clean:
	-rm -fR .dep $(BUILD_DIR)

-include $(shell mkdir .dep 2>/dev/null) $(wildcard .dep/*)

.PHONY: both clean all test test-usb

# src/usb/usbcon.c (the TinyUSB build's serial console) on the PC: a fake port and LCD, the data sheet's stream
test-usb:
	mkdir -p $(BUILD_DIR)
	gcc -std=gnu11 -Wall -O1 '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' \
	    -DCHESS47_SIM -DSERCON_TINYUSB -Itest/sim -Isrc -Isrc/usb -o $(BUILD_DIR)/usbcon_test \
	    test/usbcon_test.c src/usb/usbcon.c
	$(BUILD_DIR)/usbcon_test

# the PC simulator: real main.c + engine.c against a fake DMCP layer (test/sim); its USB writes ./acm.out, reads ./acm.in
# sercon: test/sim/sercon_sim.c plays DMCP's serial console; tinyusb: src/usb/usbcon.c over test/sim/tport_sim.c
SIM_USB = $(if $(filter sercon,$(USB)),src/sercon.c test/sim/sercon_sim.c,\
                src/sercon.c src/usb/usbcon.c test/sim/tport_sim.c)
SIM_DEFS = $(if $(filter sercon,$(USB)),,-DSERCON_TINYUSB)
sim:
	mkdir -p $(BUILD_DIR)
	gcc -O2 -w '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' -DCHESS47_SIM $(SIM_DEFS) \
	    -Itest/sim -Isrc -Isrc/usb -o $(BUILD_DIR)/sim test/sim/sim.c src/main.c src/engine.c $(SIM_USB)
.PHONY: sim
