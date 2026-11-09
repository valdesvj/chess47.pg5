# AI Assistance Policy

## How this project uses AI

This project was **developed with the help of AI** — Claude (Anthropic), Gemini (Google), and Grok (xAI) — as a cooperation. AI assisted with brainstorming, code generation, and documentation, but I reviewed all output, validated results against reference implementations, and take full responsibility for the work and any mistakes.

## Responsible use

- ✅ **I checked the results** — The move generator is checked with perft against the published counts, and the engine's strength is measured in games against Stockfish (`test/perft.c`, `test/match.py`)
- ✅ **I maintained domain expertise** — I guide the AI with the design decisions: the DM42n's memory and speed, the keys, the screens and how the game should feel on the calculator
- ✅ **I take full responsibility** — The work is mine; any errors are mine to fix
- ✅ **I'm transparent** — This file documents how AI was used

## Testing & Validation

- Perft: the legal move counts of the standard test positions, Chess960 included
- Strength: matches against Stockfish at fixed Elo levels (`test/match.py`; the results in the README)
- Every screen checked in the PC simulator (`test/sim`) before a commit
- Tested on real DM42n hardware, on battery and on USB power

## License

This does not affect the GPL-3.0-or-later license. All code remains open-source and fully accessible.

---

*For more details, see `LICENSE` and the README.*
