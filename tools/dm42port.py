#!/usr/bin/env python3
"""dm42port.py - the DM42n's serial port for the PC tools (usb_stockfish.py, lichess_board.py): Chess 47's text lines,
whichever USB the .pg5 was built with.

  DMCP5 3.58's serial console (make, the default): one byte stream, channels. A byte 0x10 + n switches to channel n;
  Chess 47's lines are on channel 2, DMCP's own (LCD rows, key reports, V) on channel 0. We send each line as 0x12
  + the line + '\\n' (DMCP passes channel 2 to the program); a line is ASCII, a binary part has the top bit set.
  TinyUSB (make USB=tinyusb): plain text lines, with the screen frames ("CHESS47 LCD 400 240" ... "END") among them.
  The 0x12 we send is not a printable character: the TinyUSB build drops it. So the same tools work with both.

  port = Port(path)            # opens it raw, read and write (opening raises DTR: DMCP's "connected")
  port.send('e7e5')            # one line to Chess 47
  for line in port.lines():    # Chess 47's lines, without '\\r\\n'; the screen and DMCP's own lines skipped
      ...
  find()                       # the DM42n in /dev/serial/by-id, else /dev/ttyACM0
"""
import glob
import os
import termios

CH = 2                                   # Chess 47's channel on the serial console


def find():
    found = sorted(glob.glob('/dev/serial/by-id/*SwissMicros*') + glob.glob('/dev/serial/by-id/*DM42*'))
    return found[0] if found else '/dev/ttyACM0'


class Port:
    def __init__(self, path):
        self.path = path
        if os.path.exists(path) and not path.startswith('/dev/'):     # a recorded stream (the simulator's acm.out)
            self.fd = os.open(path, os.O_RDONLY)
            self.tty = False
        else:
            self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
            t = termios.tcgetattr(self.fd)
            t[0] = 0; t[1] = 0; t[3] = 0                 # raw: no echo back, no line editing, no translation
            t[2] |= termios.CLOCAL | termios.CREAD
            termios.tcsetattr(self.fd, termios.TCSANOW, t)
            self.tty = True
        self.console = False                             # seen a channel byte: DMCP's serial console
        self.ch = 0

    def send(self, line):
        """one line to Chess 47: 0x12 first (the serial console's channel 2; dropped by the TinyUSB build)"""
        if self.tty:
            os.write(self.fd, bytes([0x10 + CH]) + line.encode('ascii') + b'\n')

    def lines(self):
        """Chess 47's lines; ends when the port closes (the cable out) or the file ends"""
        buf = bytearray()
        frame = False
        while True:
            chunk = os.read(self.fd, 4096)
            if not chunk:
                return
            for b in chunk:
                if 0x10 <= b <= 0x1a:                    # a channel id: never data
                    self.console = True
                    self.ch = b - 0x10
                    continue
                if b != 0x0a:
                    buf.append(b)
                    continue
                raw = bytes(buf)
                buf.clear()
                if self.console and self.ch != CH:       # DMCP's own lines (the LCD rows, V, key reports)
                    continue
                line = bytes(c for c in raw if c < 0x80).strip().decode('ascii', 'replace')
                if line.endswith('LCD 400 240'):         # TinyUSB: a screen frame, to its END
                    frame = True
                elif frame:
                    frame = line != 'END'
                elif line:
                    yield line
