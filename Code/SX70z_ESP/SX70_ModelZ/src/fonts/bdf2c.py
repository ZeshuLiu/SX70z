#!/usr/bin/env python3
"""BDF → C font converter for SSD1306 (column-based format)"""
import sys, re

def parse_bdf(path):
    glyphs = {}
    in_bitmap = False
    encoding = -1
    bitmap_rows = []
    font_w, font_h = 8, 16  # default

    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if line.startswith('FONTBOUNDINGBOX'):
                parts = line.split()
                font_w, font_h = int(parts[1]), int(parts[2])
            elif line.startswith('ENCODING'):
                encoding = int(line.split()[1])
            elif line == 'BITMAP':
                in_bitmap = True
                bitmap_rows = []
            elif line == 'ENDCHAR':
                if in_bitmap and encoding >= 0:
                    glyphs[encoding] = bitmap_rows
                in_bitmap = False
            elif in_bitmap and re.match(r'^[0-9A-Fa-f]{2}$', line):
                bitmap_rows.append(int(line, 16))
    return glyphs, font_w, font_h

def bitmap_to_column(bitmap, width, height):
    """Convert row-based BDF bitmap to column-based SSD1306 format."""
    bytes_per_col = (height + 7) // 8
    result = []
    for col in range(width):
        for b in range(bytes_per_col):
            byte_val = 0
            for row in range(8):
                r = b * 8 + row
                if r < height and (bitmap[r] >> (7 - col)) & 1:
                    byte_val |= (1 << row)
            result.append(byte_val)
    return result

def generate_c(glyphs, first, count, width, height, outpath):
    with open(outpath, 'w') as f:
        f.write(f'/* font{width}x{height}.c - Auto-generated from spleen BDF */\n\n')
        f.write(f'#include "../font.h"\n\n')

        # data array
        f.write(f'static const uint8_t font{width}x{height}_data[][{width * ((height+7)//8)}] = {{\n')
        for code in range(first, first + count):
            if code in glyphs:
                col_data = bitmap_to_column(glyphs[code], width, height)
                vals = ','.join(f'0x{b:02X}' for b in col_data)
            else:
                vals = ','.join(['0x00'] * (width * ((height + 7) // 8)))
            f.write(f'    {{{vals}}},  // 0x{code:02X} {chr(code) if 32 <= code < 127 else "?"}\n')
        f.write(f'}};\n\n')

        # font struct
        f.write(f'const ssd1306_font_t font{width}x{height}_font = {{\n')
        f.write(f'    .data = (const uint8_t *)font{width}x{height}_data,\n')
        f.write(f'    .width = {width},\n')
        f.write(f'    .height = {height},\n')
        f.write(f'    .first = {first},\n')
        f.write(f'    .count = {count},\n')
        f.write(f'}};\n')

if __name__ == '__main__':
    bdf_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else bdf_path.rsplit('.', 1)[0] + '.c'

    glyphs, font_w, font_h = parse_bdf(bdf_path)
    first = 32
    count = 95  # 32-126
    generate_c(glyphs, first, count, font_w, font_h, out_path)
    print(f"Generated {out_path} ({font_w}x{font_h}, {count} chars)")
