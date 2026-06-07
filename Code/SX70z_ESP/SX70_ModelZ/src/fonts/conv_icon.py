#!/usr/bin/env python
"""Convert LCD Assistant column-major format to row-major for draw_bitmap"""
import sys

def convert_col2row(data, w, h):
    """Convert column-major page format to row-major."""
    bpr = (w + 7) // 8  # bytes per row in output
    rows = []
    for r in range(h):
        row_bytes = [0] * bpr
        for c in range(w):
            if r < 8:
                # page0: bytes 0 to w-1
                bit = (data[c] >> (7 - r)) & 1 if c < len(data) else 0
            else:
                # page1: bytes w to w+h-1
                idx = w + c
                bit = (data[idx] >> (7 - (r - 8))) & 1 if idx < len(data) else 0
            # set bit in output row byte
            if bit:
                if c < 8:
                    row_bytes[0] |= 0x80 >> c
                else:
                    row_bytes[1] |= 0x80 >> (c - 8)
        rows.append(row_bytes)
    return rows

def fmt(data, w, h, name):
    rows = convert_col2row(data, w, h)
    out = f'// {name} ({w}x{h})\nstatic const uint8_t icon_{name}[] = {{\n'
    for row in rows:
        out += '    ' + ','.join(f'0x{b:02X}' for b in row) + ',\n'
    out += '};\n'
    return out

if __name__ == '__main__':
    # flash_14_14
    flash = [0x00,0x00,0x00,0x60,0x70,0x78,0xFE,0xFF,0xE7,0xE3,0x21,0x01,0x00,0x00,
             0x00,0x00,0x20,0x30,0x3C,0x1F,0x0F,0x07,0x01,0x00]
    print(fmt(flash, 14, 14, 'flash'))

    cross = [0x07,0x0F,0x1F,0x3E,0xFC,0xF8,0xF0,0xF0,0xF8,0xFC,0x3E,0x1F,0x0F,0x07,
             0x38,0x3C,0x3E,0x1F,0x0F,0x07,0x03,0x03,0x07,0x0F]
    print(fmt(cross, 14, 14, 'cross'))
