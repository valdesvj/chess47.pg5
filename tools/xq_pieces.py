#!/usr/bin/env python3
"""xq_pieces.py - the round Xiangqi pieces of CHESS47 (DM42n) as 1-bit bitmaps: the traditional characters drawn
with Noto Sans CJK TC Black (SIL Open Font License) in a disc. Red: a white disc in a thick ring, black character;
Black: a solid disc, white character (the screen has no colour).

    python3 tools/xq_pieces.py         -> src/xq_pieces.h (Xiangqi: the characters, the symbols, the letters),
                                          src/letters.h (chess as letters), docs/xiangqi_pieces.png (a preview)

Needs Pillow and the font (Arch: noto-fonts-cjk; Debian: fonts-noto-cjk). (C) 2026 Victor Valdes, GPL-3.0-or-later."""
import os, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZES = (23, 19)                            # the discs (pixels): zoom (the points 24 apart), normal (21 apart)
THIN = {(23, 0): 9, (23, 1): 5, (19, 0): 5, (19, 1): 3}   # the strokes' erosion (pixels at 8 times the size):
                                            # [size, colour], Black's white strokes on the disc less
RING = 1.4                                  # Red's ring (pixels)
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


def latin(px, bold=True):
    for f in LETTER_FONTS:
        f = f if bold else f.replace('-Bold', '')
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


def disc_only(black, S, scale=8):
    n = S * scale
    im = Image.new('L', (n, n), 255)
    d = ImageDraw.Draw(im)
    d.ellipse([0, 0, n - 1, n - 1], fill=0)
    if not black:
        w = int(scale * RING)
        d.ellipse([w, w, n - 1 - w, n - 1 - w], fill=255)
    return im, d


def to_bits(im, S):
    small = im.resize((S, S), Image.LANCZOS)
    return [[1 if small.getpixel((x, y)) < 128 else 0 for x in range(S)] for y in range(S)]


def disc_mask(S):
    m = Image.new('L', (S * 8, S * 8), 255)
    ImageDraw.Draw(m).ellipse([0, 0, S * 8 - 1, S * 8 - 1], fill=0)
    return to_bits(m, S)


def scaled_mask(art, box, S, chars):
    """art's pixels in chars as a mask box x box (drawn 8 times bigger and reduced: smooth at any size)"""
    h, w = len(art), max(len(r) for r in art)
    k = 8
    im = Image.new('L', (w * k, h * k), 255)
    d = ImageDraw.Draw(im)
    for y, r in enumerate(art):
        for x, c in enumerate(r):
            if c in chars:
                d.rectangle([x * k, y * k, x * k + k - 1, y * k + k - 1], fill=0)
    sc = min(box / w, box / h)
    ww, hh = max(1, round(w * sc)), max(1, round(h * sc))
    small = im.resize((ww, hh), Image.LANCZOS)
    out = [[0] * S for _ in range(S)]
    oy, ox = (S - hh) // 2, (S - ww) // 2
    for y in range(hh):
        for x in range(ww):
            out[oy + y][ox + x] = 1 if small.getpixel((x, y)) < 128 else 0
    return out


def piece_symbol(t, black, S):
    """the symbols in three tones: Red a grey figure (a 50 % checker, the screen's only grey) in a black outline on
    the white disc, Black the white figure on the black disc"""
    im, d = disc_only(black, S)
    bm = to_bits(im, S)
    art = [ART['CROWN'], ART['SHIELD'], ART['ELEPHANT'], chess_bitmap('KNIGHT'), chess_bitmap('ROOK'),
           ART['CANNON'], chess_bitmap('PAWN')][t]
    box = S - 5 if S > 20 else S - 4
    shape = scaled_mask(art, box, S, '#+')
    detail = scaled_mask(art, box, S, '+')
    for y in range(S):
        for x in range(S):
            if not shape[y][x]:
                continue
            edge = any(not (0 <= y + dy < S and 0 <= x + dx < S and shape[y + dy][x + dx])
                       for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if black:
                bm[y][x] = 1 if detail[y][x] else 0
            else:
                bm[y][x] = 1 if edge or detail[y][x] else (x + y) & 1
    return bm


def piece_letter(t, black, S):
    im, d = disc_only(black, S)
    n = S * 8
    ft = latin(int(n * 0.6), bold=black)
    ch = LETTERS[t] if not black else LETTERS[t].lower()
    bb = d.textbbox((0, 0), ch, font=ft)
    d.text(((n - (bb[2] - bb[0])) / 2 - bb[0], (n - (bb[3] - bb[1])) / 2 - bb[1]), ch, font=ft, fill=255 if black else 0)
    return to_bits(im, S)


def chess_letters():
    """The chess pieces as letters (K Q R B N P), 20 x 20 in src/pieces.h's form: the body '#' (white pieces hollow
    with an outline, black ones solid, as the figures)."""
    out = {}
    ft = latin(19, bold=True)
    for name, ch in (('PAWN', 'P'), ('KNIGHT', 'N'), ('BISHOP', 'B'), ('ROOK', 'R'), ('QUEEN', 'Q'), ('KING', 'K')):
        im = Image.new('L', (20, 20), 255)
        d = ImageDraw.Draw(im)
        bb = d.textbbox((0, 0), ch, font=ft)
        d.text(((20 - (bb[2] - bb[0])) / 2 - bb[0], (20 - (bb[3] - bb[1])) / 2 - bb[1] + 1), ch, font=ft, fill=0)
        out[name] = [''.join('#' if im.getpixel((x, y)) < 128 else '.' for x in range(20)) for y in range(20)]
    return out


def glyph(ch, n, frac, thin):
    """a character drawn n x n, its strokes made thinner (THIN: the font is Black, the heaviest Noto: eroded at 8
    times the size it reads as a Medium, one pixel strokes once reduced)"""
    t = Image.new('L', (n, n), 255)
    dt = ImageDraw.Draw(t)
    ft = font(int(n * frac))
    bb = dt.textbbox((0, 0), ch, font=ft)
    dt.text(((n - (bb[2] - bb[0])) / 2 - bb[0], (n - (bb[3] - bb[1])) / 2 - bb[1]), ch, font=ft, fill=0)
    return t.filter(ImageFilter.MaxFilter(thin)) if thin else t


def piece(ch, black, S, scale=8):
    """One piece, drawn 8 times bigger and reduced, then 1 bit (no dithering: it makes the strokes grainy)."""
    n = S * scale
    im, d = disc_only(black, S, scale)
    t = glyph(ch, n, 0.66, THIN[S, black])
    ink = t.point(lambda v: 255 if v < 128 else 0)                            # where the character is
    im.paste(255 if black else 0, mask=ink)
    return to_bits(im, S)


def main():
    out = ['/* CHESS47 for DM42n - the round Xiangqi pieces, made by tools/xq_pieces.py from Noto Sans CJK TC Black',
           ' * (thinned) and DejaVu Sans (SIL Open Font License, Bitstream Vera). Each row 3 bytes, bit 7 of byte 0 =',
           ' * the left pixel. Two sizes: XQ_PIECE (23 px, the zoom) and XQ_PIECE_N (19 px, beside the soft keys);',
           ' * [style][colour][type - 1]: style 0 the characters, 1 the symbols, 2 the letters (WXF). XQ_DISC*: the',
           ' * disc (what a piece covers). Red 帥仕相傌俥炮兵, Black 將士象馬車砲卒. GPL-3.0-or-later (the code). */',
           '#ifndef CHESS47_XQ_PIECES_H', '#define CHESS47_XQ_PIECES_H', '#include <stdint.h>',
           '#define XQ_STYLES 3', '']
    def pack(bm, S):
        b = []
        for row in bm:
            v = 0
            for x in range(24):
                v = v << 1 | (row[x] if x < S else 0)
            b += [v >> 16, v >> 8 & 255, v & 255]
        return b
    Z = 3
    preview = Image.new('1', (7 * 27 * Z + 8, 6 * 27 * Z + 2 * 6 * 23 * Z + 40), 1)
    py = 4
    for S in SIZES:
        sfx = '' if S == 23 else '_N'
        out.append('#define XQ_PSIZE%s %d' % (sfx, S))
        out.append('static const uint8_t XQ_PIECE%s[3][2][7][%d] = {' % (sfx, S * 3))
        for style in range(3):
            out.append(' {')
            for c in (0, 1):
                out.append('  {')
                for i, ch in enumerate(CHARS[c]):
                    bm = piece(ch, c, S) if style == 0 else piece_symbol(i, c, S) if style == 1 else \
                        piece_letter(i, c, S)
                    label = ch if style == 0 else ('symbol ' + LETTERS[i]) if style == 1 else LETTERS[i]
                    out.append('    { ' + ', '.join('0x%02x' % v for v in pack(bm, S)) + ' },   /* %s */' % label)
                    for y in range(S):
                        for x in range(S):
                            if bm[y][x]:
                                for dy in range(Z):
                                    for dx in range(Z):
                                        preview.putpixel((4 + i * (S + 4) * Z + x * Z + dx, py + y * Z + dy), 0)
                    if i == 6:
                        pass
                py += (S + 4) * Z
                out.append('  },')
            out.append(' },')
        out.append('};')
        out.append('static const uint8_t XQ_DISC%s[%d] = { ' % (sfx, S * 3) +
                   ', '.join('0x%02x' % v for v in pack(disc_mask(S), S)) + ' };')
        py += 8
    # the river's words 楚河 漢界, 15 x 15 (2 bytes a row), thinned as the pieces
    out.append('#define XQ_RSIZE 15')
    out.append('static const uint8_t XQ_RIVER[4][30] = {')
    for ch in '楚河漢界':
        im = glyph(ch, 120, 1.0, 5).resize((15, 15), Image.LANCZOS)
        b = []
        for y in range(15):
            v = 0
            for x in range(16):
                v = v << 1 | (1 if x < 15 and im.getpixel((x, y)) < 128 else 0)
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
    preview.crop((0, 0, preview.width, py)).save(os.path.join(ROOT, 'docs', 'xiangqi_pieces.png'))
    print('src/xq_pieces.h, src/letters.h, docs/xiangqi_pieces.png')


if __name__ == '__main__':
    main()
