"""Produce Northstar.PS4's override of colorsliders.menu.

Northstar comments out its DialogFooterButtons block with an HTML-style
comment. Valve KeyValues only understands //, so the engine reads `<!--` as a
key, `DialogFooterButtons` as its value, and then falls over on the block that
follows. This rewrites that one block as // comments; nothing else changes.
"""
import os

NL = chr(10)
SRC = ('D:/PS4/ShadPS4/CUSA04013/R2Northstar/mods/Northstar.Client/mod/'
       'resource/ui/menus/colorsliders.menu')
DST = 'mods/Northstar.PS4/mod/resource/ui/menus/colorsliders.menu'

OPEN = chr(60) + '!--'
CLOSE = '--' + chr(62)

lines = open(SRC, encoding='utf-8', errors='replace').read().split(NL)
out = []
inside = False
converted = 0

for line in lines:
    starts = OPEN in line
    ends = CLOSE in line
    if starts:
        inside = True
    if inside:
        indent = line[:len(line) - len(line.lstrip())]
        body = line.strip().replace(OPEN, '').replace(CLOSE, '').rstrip()
        out.append((indent + '// ' + body).rstrip() if body else indent + '//')
        converted += 1
        if ends:
            inside = False
        continue
    out.append(line)

header = [
    '// Northstar PS4 override of Northstar.Client/resource/ui/menus/colorsliders.menu.',
    '//',
    '// Identical to Northstar.Client' + chr(39) + 's copy except that its commented-out',
    '// DialogFooterButtons block is written with // instead of an HTML-style',
    '// comment. Valve KeyValues has no such comment form, so the engine read the',
    '// opening marker as a key and the block after it as structure:',
    '//',
    '//   KeyValues Error: RecursiveLoadFromBuffer: got } in key in file',
    '//                    resource/ui/menus/colorsliders.menu',
    '//   KeyValues Error: LoadFromBuffer: missing { in file',
    '//                    resource/ui/menus/colorsliders.menu',
    '//',
    '// Non-fatal - the menu still loads - but it corrupts the tail of the parse,',
    '// so the colour picker is built from a partly-misread layout.',
    '//',
    '// Regenerate with scripts/menus/build_colorsliders_override.py when',
    '// Northstar.Client changes.',
]

os.makedirs(os.path.dirname(DST), exist_ok=True)
open(DST, 'w', encoding='utf-8', newline=NL).write(NL.join(header + out))
print('converted %d lines' % converted)
print('wrote ' + DST)
