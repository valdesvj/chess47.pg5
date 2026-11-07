# CHESS 47 — native chess for the SwissMicros DM42n

**CHESS47.pg5** is a stand-alone DMCP5 program written in C for the
SwissMicros **DM42n**. It replaces the calculator program (Free42) with a full
chess machine that has graphic pieces, a chess clock, Chess960, 10 levels
(including **CRAZY**), save/load, PGN export, an on-screen **QR code** of the game,
and USB serial output.

Part of the "47" family of projects (almanac47, chess47, logic47, tide47 …).
This is the native C sister of the RPN `chess47` program for the C47.

![Chess 47 screens](docs/screens.png)

## Install on the DM42n

1. **Back up the calculator disk first** (connect USB, copy everything).
2. Copy `release/CHESS47.pg5` to the root of the calculator's USB disk.
   Also keep the current `DM42-x.yy.pg5` from SwissMicros there, so you can go back.
3. Open the DMCP menu: **SETUP → System → Run DMCP**, or press **RESET while holding F1**.
4. Choose **Load program → CHESS47.pg5**.

To return to Free42: in Chess 47 press **SHIFT 0 (SETUP) → System**, or use
**RESET+F1**, then **Load program → DM42-x.yy.pg5**.

> DMCP5 holds one program at a time, so loading Chess 47 replaces Free42 until you
> load the DM42 program again. Your state files on the disk are not touched.

## Keys

| Key | Action |
|---|---|
| **8 / 2 / 4 / 6** (and ▲ ▼) | move the cursor up / down / left / right |
| **7 9 1 3** | move the cursor diagonally |
| **ENTER** or **5** | select a piece / play the move |
| **EXIT** | cancel the selection; while the CPU thinks: *move now* |
| **←** (backspace) | take back (two plies vs the CPU) |
| **F1…F6** | soft keys (3 pages, `>>` switches page) |
| **+** or **−** | zoom: the board the full screen height (30 px squares), the soft keys hidden (they still work); again: the normal view |
| **SHIFT EXIT** | power off. The game is autosaved and resumed at power on |
| **SHIFT 0** | setup: time/date, beep, USB disk, **System**, About |
| **SHIFT ←** | resign |
| **SHIFT Σ+** | info screen |

Castling: select the king, then press on the **rook**. In classic games you can
also press on the king's destination square. Promotion asks Queen/Rook/Bishop/Knight.

### Soft keys

| Page | F1 | F2 | F3 | F4 | F5 | F6 |
|---|---|---|---|---|---|---|
| 1 | NEW | LEVEL | FLIP | UNDO | HINT | >> |
| 2 | 960 | CLOCK | SIDE | SAVE | LOAD | >> |
| 3 | PGN | MODE | QR | USB | INFO | >> |

* **LEVEL**: CRAZY (random legal moves, with the crazy emoticon), then 1–3 (fixed shallow
  depth plus randomness) and 4–9 (0.5 s → 30 s per move). Levels 4+ use a small opening book.
* **960**: type a Chess960 position number 0–959 and press ENTER. ENTER alone picks a random one.
  NEW then deals another random 960 position.
* **CLOCK**: OFF, 1+0, 3+2, 5+0, 10+0, 15+10, 30+0, 60+0, shown at the top left.
  The clock pauses while the calculator is off. The CPU manages its own time.
* **MODE**: You vs CPU / 2 players / CPU vs CPU demo / You vs USB / CPU vs USB (below). **SIDE** swaps colours.
* **SAVE/LOAD**: `/CHESS47/GAME.TXT`. **PGN** writes `/CHESS47/GAME.PGN`
  (with `[Variant "Chess960"]` and `[FEN]` tags when needed).
* **QR**: shows the game as a QR code. Scan it with your phone and paste it into the
  lichess analysis board.
* **USB**: turns the USB serial port on. Every move is sent as UCI text (see below), and every screen as a frame for `tools/dm42view.py` (the screen in your terminal).

## Messages

The status line always shows the game's state first: **CHECK!** (inverted) as soon as a king is attacked, even
while the computer thinks. At the end a box on the board says what happened, why, and the result:
**CHECKMATE**, **STALEMATE**, **DRAW** (50-move rule, no mating material, 3-fold repetition), **TIME** (a flag
fell), **RESIGNS**. With USB on, the result also goes to the PC as a `result 1-0 ...` line.

## Strength (measured against Stockfish)

Chess 47 (PC build, same engine) against **Stockfish 16** with `UCI_LimitStrength`
at a fixed `UCI_Elo` and 100 ms per move. Chess 47 got a **fixed node budget** per move
to imitate the calculator's speed. Each match is 20 games with alternating colours
and varied openings (random start positions for Chess960). Script: `test/run_matches.sh`.

| Imitates on the DM42n | Nodes/move | vs Stockfish | Result | Score | Est. Elo |
|---|---:|---:|---|---:|---:|
| level 6–7, battery (80 MHz) | 25 000 | 1600 | +15 =2 −3 | 80% | ~1840 |
| level 6–7, battery (80 MHz) | 25 000 | 2000 | +5 =4 −11 | 35% | ~1890 |
| level 7–8, USB power (160 MHz) | 100 000 | 2000 | +13 =4 −3 | 75% | ~2190 |
| level 7–8, USB power (160 MHz) | 100 000 | 2400 | +6 =6 −8 | 45% | ~2365 |
| **Chess960**, USB power | 100 000 | 2200 | +4 =7 −9 | 38% | ~2110 |
| level 9 (30 s), USB power | 400 000 | 2400 | +7 =8 −5 | 55% | ~2435 |

**Level 9 again (November 2026, `--level 9`, Stockfish on Arch Linux):**

| Imitates on the DM42n | Nodes/move | vs Stockfish | Result | Score | Est. Elo (95%) |
|---|---:|---:|---|---:|---:|
| level 9, USB power | 400 000 | 2200 | +9 =6 −5 | 60% | ~2270 (2145–2418) |
| level 9, USB power | 400 000 | 2400 | +11 =3 −6 | 62% | ~2489 (2351–2663) |
| level 9, USB power, **60 games** | 400 000 | 2400 | +36 =11 −13 | 69% | ~2540 (2462–2636) |
| **level 9, USB power: all 100 games** | 400 000 | 2200 + 2400 | 66 / 100 | 66% | **~2480 (±75)** |
| level 9, battery | 300 000 | 2200 | +9 =2 −9 | 50% | ~2200 (2046–2354) |
| level 9, **Chess960**, USB power | 400 000 | 2200 | +12 =3 −5 | 68% | ~2330 (2192–2518) |

The combined line is the maximum-likelihood rating over the three matches (100 games). To repeat:
`python3 test/match.py --level 9 --nodes 400000 --elo 2400 --games 60`.

**Roughly 1850 on battery at the middle levels, 2200–2300 on USB power, about 2480 at level 9 on USB power (about 2200 on battery).**

Please read these as rough estimates:
* 20 games per match means about ±100 Elo of statistical error.
* Stockfish's `UCI_Elo` is calibrated at longer time controls. At 100 ms per move it plays
  somewhat below its nominal rating, so the absolute numbers are probably **optimistic by
  100–200 points**. Treat it as a strong club player, not a master.
* The DM42n speed (~10k / ~19k nodes/s) is estimated from instruction counts, not measured
  on hardware yet. The calculator's INFO screen shows the real nodes/s after a search.


## Stockfish on Linux over USB (both ways)

MODE (menu page 3) has **You vs USB**: the other side is played by a program on the PC, over the USB serial port
(MODE switches USB on). SIDE chooses your colour. The calculator sends `newgame <FEN>`, `opponent white|black` and
every move (`e2e4`); the PC answers with its move (`e7e5`), which the calculator checks, plays and sends back as the
confirmation (`illegal ...` / `notyourturn` otherwise). `msg <text>` shows a text in the status line.

```bash
sudo pacman -S stockfish python-chess
python3 tools/usb_stockfish.py --elo 2000 --time 2      # finds the DM42n by name; Stockfish plays the USB side
python3 tools/usb_stockfish.py --manual                  # the old way: you type Stockfish's move ("2 players")
```

### Lichess: the DM42n as your board

`tools/lichess_board.py` connects "You vs USB" to Lichess's **Board API**: you play your moves on the calculator, they
go to Lichess as yours, and your opponent's come back to the board. The PC also sends `newgame white|black [FEN]`
(the calculator starts that game, your side at the bottom), `play <move>` (to catch up a game in progress) and
`msg` lines (the opponent and the clocks). Hints are off in "You vs USB": in games as yourself only you choose the
moves (Lichess fair play; engines play only on BOT accounts).

```bash
mkdir -p ~/.config/chess47      # a token with the scope "Play games with the board API" (board:play):
echo 'lip_...' > ~/.config/chess47/lichess.token && chmod 600 ~/.config/chess47/lichess.token
python3 tools/lichess_board.py --ai 3           # against Lichess's computer (levels 1-8)
python3 tools/lichess_board.py --seek           # a casual 15+10 game against a person
python3 tools/lichess_board.py --challenge NAME # a friend
```

### Lichess: Chess 47 as a BOT (the calculator thinks)

**CPU vs USB**: the calculator's own engine plays one side and the USB the other. `lichess_board.py --bot` puts it on
a Lichess **BOT account**: it accepts challenges (or sends one), the calculator thinks every move and the bridge sends
it to Lichess. Before each of its moves the PC sends the game's clock (`clock <ms left> <increment ms>`); the engine
uses about 1/30 of its time, never more than its level's time (level 9: 30 s). The PC also sends `level N`,
`botgame white|black [FEN]` (the engine's side), `play` lines to catch up, `go` (the engine may move) and
`end <reason> <result>` when the game ends on Lichess (resignation, time, abort).

A BOT account is a **new** Lichess account that has never played a game, upgraded once (it can't be undone, so not
your own account), with a token of the scope **bot:play**:

```bash
echo 'lip_...' > ~/.config/chess47/bot.token && chmod 600 ~/.config/chess47/bot.token
curl -d '' https://lichess.org/api/bot/account/upgrade -H "Authorization: Bearer $(cat ~/.config/chess47/bot.token)"
# on the DM42n: Chess 47, USB on (page 3, F4)
python3 tools/lichess_board.py --bot --token ~/.config/chess47/bot.token                  # accept challenges
python3 tools/lichess_board.py --bot --token ~/.config/chess47/bot.token --challenge NAME  # challenge someone
#   --level 0-9 (default 9)   --speeds blitz,rapid,classical,correspondence (the challenges accepted)
```

Keep the DM42n on USB power (the engine is about twice as fast). Bullet is declined by default: the USB and the
network take about a second a move. For a bot without the calculator (the same engine on the PC), use
`build/chess47-uci` with [lichess-bot](https://github.com/lichess-bot-devs/lichess-bot).


## The screen in your terminal

With USB on, every new screen also goes to the PC (`CHESS47 LCD 400 240`, 240 lines of hex, `END`; the moves still
come as plain text lines between them). `tools/dm42view.py` (the same viewer as NAV47) shows it:

```bash
python3 tools/dm42view.py                 # Braille dots in the terminal
python3 tools/dm42view.py --png shots/    # and a PNG of every screen
```

The firmware's own `usb_acm_on()` leaves the USB port disconnected (the DM42n's USB core stays in soft-disconnect),
so Chess 47 drives the USB itself with **TinyUSB** (`src/usb/`, `third_party/tinyusb`, MIT): one CDC serial port,
`/dev/ttyACM0`, which can also **receive** (for a two-way Stockfish link later). While USB is on the calculator does
not sleep (it would stop the USB). Chess 47 learns how the firmware stores the LCD lines (it sets a pixel and looks
which bit changes).

## Build

Requirements: `arm-none-eabi-gcc` (Debian/Ubuntu: `apt install gcc-arm-none-eabi libnewlib-arm-none-eabi`), `make`, `python3`.

```bash
make            # -> build/CHESS47.pg5
make test       # PC: perft (move generator incl. Chess960) + search tests
```

`tools/pg5tool.py` replaces the SDK's bash helpers. It writes the QSPI CRC header,
then packs the `.pg5`: program size word plus SHA1 trailer.

### PC simulator and UCI engine

```bash
# simulator: real main.c + engine.c against a fake DMCP layer, renders the LCD
gcc -O2 -w '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' \
    -Itest/sim -Isrc -Isrc/usb -o build/sim test/sim/sim.c src/main.c src/engine.c src/usbview.c test/sim/usbser_sim.c
cd build && ./sim 5 8 8 5 WAIT:2000 SNAP:shot && python3 ../test/sim/render.py shot shot.png

# UCI engine (for cutechess, Arena, or the Elo test)
gcc -O2 -o build/chess47-uci test/uci.c src/engine.c
GAMES=20 test/run_matches.sh
```

## Inside

| | |
|---|---|
| Board | 0x88, copy-make, Zobrist hashing |
| Moves | fully legal, en passant, promotions, **Chess960 castling** (X-FEN/Shredder-FEN), perft-verified |
| Search | iterative deepening, PVS alpha-beta, transposition table, null move, LMR, check extension, killers + history, quiescence with delta pruning, repetition/50-move detection |
| Eval | material + piece-square tables (tapered), mobility, pawn structure (passed/doubled/isolated), rooks on open files, bishop pair, development, king pawn shield |
| Memory | 2.4 KB of the 8 KB static RAM. Everything else (≈85 KB engine + hash table sized from free heap + 70 KB game history) is on the heap |
| Speed | about 5,000 instructions per node, so an estimated ~10k nodes/s on battery (80 MHz) and ~19k nodes/s on USB power (160 MHz) |

Files: `src/engine.c` (portable engine), `src/main.c` (DMCP5 UI), `src/pieces.h` (bitmaps),
`test/` (perft, simulator, UCI, match), `tools/` (pg5 packer, USB-Stockfish bridge).

## License

Written with the help of AI. See `AI_ASSISTANCE.md` for how it was used and checked.

MIT © 2026 Victor Valdes. `dmcp/` and `stm32_program.ld` are from the SwissMicros
DMCP5 SDK (BSD-3, released "NOMAS": not manufacturer supported). Please don't
contact SwissMicros about this program.
