#!/usr/bin/env python3
"""xq_pieces.py - the round Xiangqi pieces of CHESS47 (DM42n) as 1-bit bitmaps: the traditional characters drawn
with Noto Sans CJK TC Black (SIL Open Font License) in a disc. Red: a white disc in a thick ring, black character;
Black: a solid disc, white character (the screen has no colour).

    python3 tools/xq_pieces.py         -> src/xq_pieces.h (Xiangqi: the characters, the symbols, the letters),
                                          src/letters.h (chess as letters), docs/xiangqi_pieces.png (a preview)

Needs Pillow and the font (Arch: noto-fonts-cjk; Debian: fonts-noto-cjk). (C) 2026 Victor Valdes, GPL-3.0-or-later."""
import os, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 23                                   # the disc (pixels), the board's points 24 apart
CHARS = {0: '帥仕相傌俥炮兵', 1: '將士象馬車砲卒'}  # Red, Black: general advisor elephant horse chariot cannon soldier
FONTS = ['/usr/share/fonts/noto-cjk/NotoSansCJK-Black.ttc',            # Arch (heavier strokes: they read better at 23 px)
         '/usr/share/fonts/opentype/noto/NotoSansCJK-Black.ttc',      # Debian, Ubuntu
         '/usr/share/fonts/google-noto-cjk/NotoSansCJK-Black.ttc']    # Fedora
TC = 1                                      # the collection's face: 0 JP, 1 KR, 2 SC, 3 TC, 4 HK (Noto's order)


# the symbols (style 2): drawn here; the horse, chariot and soldier are chess's knight, rook and pawn (src/pieces.h)
ART = {
    'CROWN': ['.................', '..#....#....#....', '..##..###..##....', '..##..###..##....', '..###.###.###....',
              '..###########....', '..###########....', '...#########.....', '...#.#.#.#.#.....', '...#########.....',
              '..###########....', '.................'],
    'SHIELD': ['...###########...', '...#.........#...', '...#.##...##.#...', '...#..##.##..#...', '...#...###...#...',
               '...#...###...#...', '...#..##.##..#...', '....#.##.##.#....', '....#.......#....', '.....#.....#.....',
               '......#...#......', '.......###.......', '........#........'],
    'ELEPHANT': ['.....#######.....', '...###########...', '..#############..', '.####..###..####.', '.###.#.###.#.###.',
                 '.###..#####..###.', '.####.#####.####.', '..###.#####.###..', '...#..#####..#...', '......#####......',
                 '.......###.......', '.......###.......', '........##.......', '.........##......', '..........##.....'],
    'CANNON': ['.............###.', '...........######', '.........#######.', '.......#######...', '.....#######.....',
               '...#######.......', '..######.........', '..#####..........', '...###.###.......', '.....#######.....',
               '....##.#.#.##....', '....#..###..#....', '....##.#.#.##....', '.....#######.....', '.......###.......'],
}
LETTERS = 'KAEHRCP'
LETTER_FONTS = ['/usr/share/fonts/TTF/DejaVuSans-Bold.ttf',                  # Arch (ttf-dejavu)
                '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf']      # Debian, Ubuntu


def latin(px):
    for f in LETTER_FONTS:
        if os.path.exists(f):
            return ImageFont.truetype(f, px)
    raise SystemExit('no DejaVu Sans Bold: pacman -S ttf-dejavu')


def chess_bitmap(name):
    import re
    src = open(os.path.join(ROOT, 'src', 'pieces.h')).read()
    m = re.search(r'/\* %s \*/\s*((?:"[^"]*",?\s*)+)\}' % name, src)
    return [''.join('#' if c in '#+' else '.' for c in r) for r in re.findall(r'"([^"]*)"', m.group(1))]


def font(px):
    for f in FONTS:
        if os.path.exists(f):
            for idx in range(5):
                ft = ImageFont.truetype(f, px, index=idx)
                if 'TC' in ' '.join(ft.getname()):
                    return ft
            return ImageFont.truetype(f, px, index=3)
    raise SystemExit('no Noto Sans CJK Black: pacman -S noto-fonts-cjk')


def disc_only(black, scale=4):
    n = SIZE * scale
    im = Image.new('L', (n, n), 255)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 0, n - 1, n - 1], fill=0)
    if not black:
        w = int(scale * 1.6)
        d.ellipse([w, w, n - 1 - w, n - 1 - w], fill=255)
    return im, d


def to_bits(im):
    small = im.resize((SIZE, SIZE), Image.LANCZOS)
    return [[1 if small.getpixel((x, y)) < 128 else 0 for x in range(SIZE)] for y in range(SIZE)]


def piece_symbol(t, black):
    im, d = disc_only(black, 1)
    bm = [[1 if im.getpixel((x, y)) < 128 else 0 for x in range(SIZE)] for y in range(SIZE)]
    art = [ART['CROWN'], ART['SHIELD'], ART['ELEPHANT'], chess_bitmap('KNIGHT'), chess_bitmap('ROOK'),
           ART['CANNON'], chess_bitmap('PAWN')][t]
    h, w = len(art), max(len(r) for r in art)
    sc = min(17 / w, 17 / h, 1.0)
    hh, ww = int(h * sc), int(w * sc)
    oy, ox = (SIZE - hh) // 2, (SIZE - ww) // 2
    for y in range(hh):
        for x in range(ww):
            r = art[int(y / sc)]
            if int(x / sc) < len(r) and r[int(x / sc)] == '#':
                bm[oy + y][ox + x] = 0 if black else 1
    return bm


def piece_letter(t, black):
    im, d = disc_only(black)
    n = SIZE * 4
    ft = latin(int(n * 0.62))
    ch = LETTERS[t] if not black else LETTERS[t].lower()
    bb = d.textbbox((0, 0), ch, font=ft)
    d.text(((n - (bb[2] - bb[0])) / 2 - bb[0], (n - (bb[3] - bb[1])) / 2 - bb[1]), ch, font=ft, fill=255 if black else 0)
    return to_bits(im)


def chess_letters():
    """The chess pieces as letters (K Q R B N P), 20 x 20 in src/pieces.h's form: the body '#' (white pieces hollow
    with an outline, black ones solid, as the figures)."""
    out = {}
    ft = latin(19)
    for name, ch in (('PAWN', 'P'), ('KNIGHT', 'N'), ('BISHOP', 'B'), ('ROOK', 'R'), ('QUEEN', 'Q'), ('KING', 'K')):
        im = Image.new('L', (20, 20), 255)
        d = ImageDraw.Draw(im)
        bb = d.textbbox((0, 0), ch, font=ft)
        d.text(((20 - (bb[2] - bb[0])) / 2 - bb[0], (20 - (bb[3] - bb[1])) / 2 - bb[1] + 1), ch, font=ft, fill=0)
        out[name] = [''.join('#' if im.getpixel((x, y)) < 128 else '.' for x in range(20)) for y in range(20)]
    return out


def piece(ch, black, scale=4):
    """One piece, drawn 4 times bigger and reduced (smoother edges), then 1 bit."""
    n = SIZE * scale
    im = Image.new('L', (n, n), 255)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 0, n - 1, n - 1], fill=0)                                  # the disc's edge
    if black:
        inner = 255
        ft = font(int(n * 0.66))
        bb = d.textbbox((0, 0), ch, font=ft)
        x = (n - (bb[2] - bb[0])) / 2 - bb[0]; y = (n - (bb[3] - bb[1])) / 2 - bb[1]
        d.text((x, y), ch, font=ft, fill=inner)
    else:
        w = int(scale * 1.6)
        d.ellipse([w, w, n - 1 - w, n - 1 - w], fill=255)                     # white inside a thick ring
        ft = font(int(n * 0.64))
        bb = d.textbbox((0, 0), ch, font=ft)
        x = (n - (bb[2] - bb[0])) / 2 - bb[0]; y = (n - (bb[3] - bb[1])) / 2 - bb[1]
        d.text((x, y), ch, font=ft, fill=0)
    small = im.resize((SIZE, SIZE), Image.LANCZOS)
    mask = Image.new('L', (SIZE, SIZE), 255)                                  # outside the disc: transparent
    ImageDraw.Draw(mask).ellipse([0, 0, SIZE - 1, SIZE - 1], fill=0)
    return [[1 if small.getpixel((x, y)) < 128 else 0 for x in range(SIZE)] for y in range(SIZE)], \
           [[1 if mask.getpixel((x, y)) < 128 else 0 for x in range(SIZE)] for y in range(SIZE)]


def main():
    rows = []
    out = ['/* CHESS47 for DM42n - the round Xiangqi pieces, %d x %d, made by tools/xq_pieces.py from Noto Sans CJK'
           % (SIZE, SIZE),
           ' * TC Black (SIL Open Font License). Each row 3 bytes, bit 7 of byte 0 = the left pixel. XQ_PIECE[colour]'
           '[type - 1]: the',
           ' * ink; XQ_DISC: the disc (what a piece covers). Red: general advisor elephant horse chariot cannon '
           'soldier',
           ' * 帥仕相傌俥炮兵, Black 將士象馬車砲卒. GPL-3.0-or-later (the code), the glyphs from the font: OFL. */',
           '#ifndef CHESS47_XQ_PIECES_H', '#define CHESS47_XQ_PIECES_H', '#include <stdint.h>',
           '#define XQ_PSIZE %d' % SIZE, '']
    def pack(bm):
        b = []
        for row in bm:
            v = 0
            for x in range(24):
                v = v << 1 | (row[x] if x < SIZE else 0)
            b += [v >> 16, v >> 8 & 255, v & 255]
        return b
    preview = Image.new('1', (SIZE * 7 * 4 + 8 * 4, SIZE * 2 * 4 + 12), 1)
    out.append('/* XQ_PIECE[style][colour][type - 1]: style 0 the characters, 1 the symbols, 2 the letters (WXF) */')
    out.append('#define XQ_STYLES 3')
    out.append('static const uint8_t XQ_PIECE[3][2][7][%d] = {' % (SIZE * 3))
    disc = None
    for style in range(3):
        out.append(' {')
        for c in (0, 1):
            out.append('  {')
            for i, ch in enumerate(CHARS[c]):
                if style == 0:
                    bm, disc = piece(ch, c)
                elif style == 1:
                    bm = piece_symbol(i, c)
                else:
                    bm = piece_letter(i, c)
                label = ch if style == 0 else ('symbol ' + LETTERS[i]) if style == 1 else LETTERS[i]
                out.append('    { ' + ', '.join('0x%02x' % v for v in pack(bm)) + ' },   /* %s */' % label)
                if style == 0:
                    for y in range(SIZE):
                        for x in range(SIZE):
                            if bm[y][x]:
                                for dy in range(4):
                                    for dx in range(4):
                                        preview.putpixel((4 + i * (SIZE * 4 + 4) + x * 4 + dx, 4 + c * (SIZE * 4 + 4) + y * 4 + dy), 0)
            out.append('  },')
        out.append(' },')
    out.append('};')
    out.append('static const uint8_t XQ_DISC[%d] = { ' % (SIZE * 3) + ', '.join('0x%02x' % v for v in pack(disc)) + ' };')
    # the river's words 楚河 漢界, 15 x 15 (2 bytes a row)
    out.append('#define XQ_RSIZE 15')
    out.append('static const uint8_t XQ_RIVER[4][30] = {')
    ft = font(15)
    for ch in '楚河漢界':
        im = Image.new('L', (15, 15), 255)
        d = ImageDraw.Draw(im)
        bb = d.textbbox((0, 0), ch, font=ft)
        d.text(((15 - (bb[2] - bb[0])) / 2 - bb[0], (15 - (bb[3] - bb[1])) / 2 - bb[1]), ch, font=ft, fill=0)
        b = []
        for y in range(15):
            v = 0
            for x in range(16):
                v = v << 1 | (1 if x < 15 and im.getpixel((x, y)) < 140 else 0)
            b += [v >> 8, v & 255]
        out.append('  { ' + ', '.join('0x%02x' % v for v in b) + ' },   /* %s */' % ch)
    out.append('};')
    out += ['', '#endif', '']
    # the chess letters
    cl = chess_letters()
    L = ['/* CHESS47 for DM42n - the chess pieces as letters (the key x), made by tools/xq_pieces.py from DejaVu Sans Bold',
         ' * (its license: free, Bitstream Vera). The form of PIECE_BMP in pieces.h. GPL-3.0-or-later. */',
         '#ifndef CHESS47_LETTERS_H', '#define CHESS47_LETTERS_H', '',
         'static const char * const LETTER_BMP[7][PIECE_H] = {', '  { 0 },']
    for name in ('PAWN', 'KNIGHT', 'BISHOP', 'ROOK', 'QUEEN', 'KING'):
        L.append('  { /* %s */' % name)
        L += ['    "%s",' % r for r in cl[name]]
        L.append('  },')
    L += ['};', '', '#endif', '']
    open(os.path.join(ROOT, 'src', 'letters.h'), 'w').write('\n'.join(L))
    open(os.path.join(ROOT, 'src', 'xq_pieces.h'), 'w', encoding='utf-8').write('\n'.join(out))
    os.makedirs(os.path.join(ROOT, 'docs'), exist_ok=True)
    preview.save(os.path.join(ROOT, 'docs', 'xiangqi_pieces.png'))
    print('src/xq_pieces.h, src/letters.h, docs/xiangqi_pieces.png')


if __name__ == '__main__':
    main()
