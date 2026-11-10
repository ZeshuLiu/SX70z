#!/usr/bin/env python3
"""Convert the archived BDF ASCII glyphs into the SSD1306 column format.

Run from any directory: python tools/convert_terminus8x14.py [--check]
--check verifies the checked-in C file without changing it.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
BDF = ROOT / 'docs/oled-portrait/fonts/ter-u14b.bdf'
OUTPUT = ROOT / 'src/fonts/font_terminus8x14.c'


def convert():
    source = BDF.read_text(encoding='ascii')
    ascent = int(re.search(r'^FONT_ASCENT (\d+)$', source, re.M)[1])
    descent = int(re.search(r'^FONT_DESCENT (\d+)$', source, re.M)[1])
    if (ascent, descent) != (12, 2):
        raise ValueError('Unexpected font metrics')
    glyphs = {}
    for block in re.findall(r'^STARTCHAR [^\n]+\n(.*?)^ENDCHAR$', source, re.M | re.S):
        code = int(re.search(r'^ENCODING (-?\d+)$', block, re.M)[1])
        if not 32 <= code <= 126:
            continue
        if code in glyphs:
            raise ValueError(f'Duplicate encoding {code}')
        advance = tuple(map(int, re.search(r'^DWIDTH (.+)$', block, re.M)[1].split()))
        width, height, x_offset, y_offset = map(int, re.search(r'^BBX (.+)$', block, re.M)[1].split())
        rows = re.search(r'^BITMAP\n(.*)$', block, re.M | re.S)[1].strip().splitlines()
        if advance != (8, 0) or len(rows) != height:
            raise ValueError(f'Unexpected glyph dimensions at {code}')
        top = ascent - y_offset - height
        if x_offset < 0 or top < 0 or x_offset + width > 8 or top + height > 14:
            raise ValueError(f'Glyph {code} extends outside the cell')
        matrix = [[0] * 8 for _ in range(14)]
        for y, row in enumerate(rows):
            bits = int(row, 16)
            for x in range(width):
                matrix[top + y][x_offset + x] = (bits >> (len(row) * 4 - 1 - x)) & 1
        packed = [sum(matrix[y][x] << (y % 8) for y in range(page * 8, min(page * 8 + 8, 14)))
                  for x in range(8) for page in range(2)]
        # Compare every decoded pixel to the source matrix, including cell padding.
        assert all(((packed[x * 2 + y // 8] >> (y % 8)) & 1) == matrix[y][x]
                   for y in range(14) for x in range(8))
        glyphs[code] = packed
    if set(glyphs) != set(range(32, 127)):
        raise ValueError('Incomplete ASCII glyph set')
    lines = [
        '/* Generated from ter-u14b.bdf by tools/convert_terminus8x14.py.',
        ' * Copyright (C) 2020 Dimitar Toshkov Zhekov.',
        ' * Licensed under SIL OFL 1.1; see TERMINUS-LICENSE.txt.',
        ' * Format-converted ASCII subset: 8x14 cells, 12 ascent, 2 descent.',
        ' */', '', '#include "font_terminus8x14.h"', '',
        'static const uint8_t font_terminus8x14_data[][16] = {'
    ]
    for code in range(32, 127):
        values = ', '.join(f'0x{x:02X}' for x in glyphs[code])
        lines.append(f'    {{{values}}}, /* 0x{code:02X} */')
    lines += ['};', '', 'const ssd1306_font_t font_terminus8x14_font = {',
              '    .data = (const uint8_t *)font_terminus8x14_data,',
              '    .width = 8,', '    .height = 14,', '    .first = 32,',
              '    .count = 95,', '};', '']
    return '\n'.join(lines)


def main():
    if sys.argv[1:] not in ([], ['--check']):
        raise SystemExit('Usage: python tools/convert_terminus8x14.py [--check]')
    generated = convert()
    if sys.argv[1:]:
        if OUTPUT.read_bytes() != generated.encode('ascii'):
            raise SystemExit('Generated output differs from checked-in file')
        print('Verified 95 glyphs pixel by pixel; output is reproducible; digit advance is 8 px.')
    else:
        OUTPUT.write_bytes(generated.encode('ascii'))
        print(f'Generated {OUTPUT}')


if __name__ == '__main__':
    main()
