#!/usr/bin/env python3
"""c47keys.py - C47 commands -> the DM42n's keys to press, for dm42view.py --c47 (DMCP5 3.58's serial console
presses them: the C47 firmware on the DM42n gets them as real keys).

The keyboard comes from the C47 firmware source (gitlab rpncalculators/c43, your clone ~/opt/c43):
  src/c47/assign.c   kbd_std[]: the 37 keys in DMCP's order (Σ+ ... +), each {keyId, primary, f-shifted,
                     g-shifted, ...}: an item (ITM_SIN), a menu (-MNU_TRI) or a digit / key code
The names come from Almanac 47's table of the same firmware (docs/reference/C47_items_master.tsv, tools/c47ref.py):
catalog name, softmenu name, rejig name and aliases, and the ITM_ name without ITM_ (SIN, SQUARE, STO ...).

  python3 tools/c47keys.py --layout                    # the keyboard as read: check it against your calculator
  python3 tools/c47keys.py "2 ENTER 3 + SIN STO 01"    # -> 2 ENTER 3 ADD SIN STO 0 1
  python3 tools/c47keys.py --find SQRT                 # where a command is

What a line may hold (words with spaces):
  numbers      2  -2.5  1.5E-3   the digit keys, DOT, E (EEX), CHS for a minus (after the digits, as on the C47)
  commands     any item on a key, primary, f or g: SIN, STO, x^2 / SQUARE (the names above), case-insensitive
  parameters   after a command with one (STO, RCL, XEQ ... TM_* in the table): its digits: STO 01 -> STO 0 1
  keys         k:NAME presses one key as it is (k:EXIT, k:F1, k:SHIFT, k:UP); WAIT:ms waits
A command that is only in a menu (not on a key) is reported: open its menu with its key, then k:F1..k:F6.

f and g: on the DM42n the C47 has one shift key (SHIFT, the yellow key). --f / --g say which keys give f and g
(default f = SHIFT, g = SHIFT SHIFT); check with --layout and your C47's manual.
"""
import argparse
import os
import re
import sys

ORDER = ['SIGMA', 'INV', 'SQRT', 'LOG', 'LN', 'XEQ', 'STO', 'RCL', 'RDN', 'SIN', 'COS', 'TAN', 'ENTER', 'SWAP',
         'CHS', 'E', 'BSP', 'UP', '7', '8', '9', 'DIV', 'DOWN', '4', '5', '6', 'MUL', 'SHIFT', '1', '2', '3', 'SUB',
         'EXIT', '0', 'DOT', 'RUN', 'ADD']                 # DMCP's key codes 1..37 (dmcp.h), dm42view.KEYS
SHIFTS = {'f': ['SHIFT'], 'g': ['SHIFT', 'SHIFT']}
NUMBER = re.compile(r'^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$')


def c43_dir(arg=None):
    return os.path.expanduser(arg or os.environ.get('C43_DIR') or '~/opt/c43')


def items_table():
    """ITM_ name -> its names (catalog, softmenu, rejig, aliases), and whether it takes a parameter"""
    here = os.path.dirname(os.path.abspath(__file__))
    for p in (os.environ.get('C47_ITEMS', ''), os.path.join(here, '..', '..', 'almanac47', 'docs', 'reference',
              'C47_items_master.tsv'), os.path.expanduser('~/opt/almanac47/docs/reference/C47_items_master.tsv')):
        if p and os.path.exists(p):
            break
    else:
        return {}
    out = {}
    cols = None
    for line in open(p, encoding='utf-8'):
        if line.startswith('#'):
            continue
        f = line.rstrip('\n').split('\t')
        if cols is None:
            cols = {c: i for i, c in enumerate(f)}
            continue
        get = lambda c: f[cols[c]] if cols.get(c) is not None and cols[c] < len(f) else ''
        itm = get('itm')
        names = {get('catalog_name'), get('softmenu_name'), get('rejig_name')} | set(get('rejig_aliases').split())
        out[itm] = ({n for n in names if n}, get('param').startswith('TM_'))
    return out


def braces(text):
    """the top-level {...} groups of an array initialiser, each as its fields"""
    groups, depth, cur = [], 0, ''
    for ch in text:
        if ch == '{':
            depth += 1
            if depth == 1:
                cur = ''
                continue
        elif ch == '}':
            depth -= 1
            if depth == 0:
                groups.append([x.strip() for x in cur.split(',')])
                continue
        if depth >= 1:
            cur += ch
    return groups


def layout(c43=None):
    """{name: (key name, shift '' / 'f' / 'g')} from assign.c's kbd_std (and the item names)"""
    path = os.path.join(c43_dir(c43), 'src', 'c47', 'assign.c')
    if not os.path.exists(path):
        sys.exit('c47keys: no %s (the C47 firmware source: git clone https://gitlab.com/rpncalculators/c43 ~/opt/c43)'
                 % path)
    src = open(path, encoding='utf-8', errors='replace').read()
    src = re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', src, flags=re.S))
    m = re.search(r'kbd_std\s*\[[^\]]*\]\s*=\s*\{', src)
    if not m:
        sys.exit('c47keys: no kbd_std[] in %s' % path)
    depth, i = 1, m.end()
    while depth:
        depth += {'{': 1, '}': -1}.get(src[i], 0)
        i += 1
    keys = braces(src[m.end():i - 1])
    if len(keys) != len(ORDER):
        sys.exit('c47keys: kbd_std has %d keys, not %d: the layout is not as expected' % (len(keys), len(ORDER)))
    table = items_table()
    lay, raw = {}, []
    for key, k in zip(ORDER, keys):
        row = {}
        for shift, field in (('', 1), ('f', 2), ('g', 3)):
            tok = k[field] if field < len(k) else ''
            row[shift] = tok
            if not tok.startswith('ITM_'):
                continue                                  # a menu (-MNU_...), a digit or nothing
            names = {tok[4:]} | table.get(tok, (set(), False))[0]
            for n in names:
                lay.setdefault(n.upper(), (key, shift, table.get(tok, (set(), False))[1]))
        raw.append((key, row))
    lay['_raw'] = raw
    return lay


def number(t):
    """a number -> the digit keys (DOT, E, CHS after the mantissa / the exponent)"""
    neg = t.startswith('-')
    t = t.lstrip('+-').upper()
    mant, _, exp = t.partition('E')
    out = [{'.': 'DOT'}.get(c, c) for c in mant]
    if neg:
        out.append('CHS')
    if exp:
        out.append('E')
        out += [c for c in exp.lstrip('+-')]
        if exp.startswith('-'):
            out.append('CHS')
    return out


def translate(text, lay, f=None, g=None):
    shifts = {'': [], 'f': f or SHIFTS['f'], 'g': g or SHIFTS['g']}
    out, words, i = [], text.split(), 0
    while i < len(words):
        w = words[i]
        i += 1
        if w.upper().startswith('WAIT:'):
            out.append(w)
        elif w.lower().startswith('k:'):
            out.append(w[2:].upper())
        elif NUMBER.match(w):
            out += number(w)
        else:
            hit = lay.get(w.upper())
            if not hit:
                raise SystemExit('c47keys: %r is not on a key (primary, f or g): open its menu with k:..., then k:F1-F6'
                                 % w)
            key, shift, param = hit
            out += shifts[shift] + [key]
            if param and i < len(words) and re.fullmatch(r'\d+|\.\d+', words[i]):
                out += [{'.': 'DOT'}.get(c, c) for c in words[i]]     # the parameter: STO 01, XEQ 10, STO .5
                i += 1
    return out


def main():
    ap = argparse.ArgumentParser(description='C47 commands -> DM42n keys (for dm42view.py --c47)')
    ap.add_argument('text', nargs='?', help='C47 commands, e.g. "2 ENTER 3 + SIN STO 01"')
    ap.add_argument('--c43', help='the C47 firmware source (default $C43_DIR or ~/opt/c43)')
    ap.add_argument('--layout', action='store_true', help='print the keyboard as read from assign.c')
    ap.add_argument('--find', help='where a command is')
    ap.add_argument('--f', default='SHIFT', help='the keys for f (default SHIFT)')
    ap.add_argument('--g', default='SHIFT SHIFT', help='the keys for g (default SHIFT SHIFT)')
    a = ap.parse_args()
    lay = layout(a.c43)
    if a.layout:
        for key, row in lay['_raw']:
            print('%-6s  %-22s f %-22s g %s' % (key, row[''], row['f'], row['g']))
    if a.find:
        hit = lay.get(a.find.upper())
        print(a.find, '->', ('%s%s' % (hit[1] + ' ' if hit[1] else '', hit[0])) if hit else 'not on a key')
    if a.text:
        print(' '.join(translate(a.text, lay, a.f.split(), a.g.split())))


if __name__ == '__main__':
    main()
