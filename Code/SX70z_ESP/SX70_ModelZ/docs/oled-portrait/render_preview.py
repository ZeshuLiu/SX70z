"""Reproduce the discussion previews; this does not build or modify firmware."""
from pathlib import Path
import re
from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[1]


def load_c_font(name, width, height):
    source = (ROOT / 'src/fonts' / (name + '.c')).read_text(encoding='utf-8')
    source = source.split('= {', 1)[1].split('};', 1)[0]
    data = [[int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', row)]
            for row in re.findall(r'\{([^{}]+)\}', source)]
    return width, height, data


def load_terminus():
    source = (OUT / 'fonts/ter-u14b.bdf').read_text(encoding='latin1')
    glyphs = {}
    for block in re.findall(r'STARTCHAR [^\n]+\n(.*?)ENDCHAR', source, re.S):
        code = int(re.search(r'^ENCODING (-?\d+)', block, re.M)[1])
        if not 32 <= code < 127:
            continue
        advance = int(re.search(r'^DWIDTH (\d+)', block, re.M)[1])
        width, height, dx, dy = map(int, re.search(r'^BBX ([^\n]+)', block, re.M)[1].split())
        rows = re.search(r'BITMAP\s*\n(.*)', block, re.S)[1].strip().splitlines()
        assert len(rows) == height
        glyph = Image.new('1', (width, height))
        for y, row in enumerate(rows):
            bits = int(row, 16)
            for x in range(width):
                if bits & (1 << (len(row) * 4 - 1 - x)):
                    glyph.putpixel((x, y), 1)
        glyphs[chr(code)] = advance, dx, dy, glyph
    return glyphs


F5 = load_c_font('font5x8', 5, 8)
F6 = load_c_font('font6x12', 6, 12)
TERMINUS = load_terminus()


def render(mode='AUTO', flash=False, wifi=False, ble=False, timed_seconds=10):
    im = Image.new('1', (32, 128))
    draw = ImageDraw.Draw(im)

    def text(value, y, font=F5, x=None, ink=1):
        width, height, data = font
        x = (32 - len(value) * width) // 2 if x is None else x
        assert 0 <= x and x + len(value) * width <= 32 and y + height <= 128
        for n, char in enumerate(value):
            glyph = data[ord(char) - 32]
            for col in range(width):
                for row in range(height):
                    if glyph[col * ((height + 7) // 8) + row // 8] & (1 << (row % 8)):
                        im.putpixel((x + n * width + col, y + row), ink)

    def main_value(value):
        width = sum(TERMINUS[c][0] for c in value)
        assert width <= 32
        strip = Image.new('1', (width, 64))
        x = 0
        for char in value:
            advance, dx, dy, glyph = TERMINUS[char]
            strip.paste(glyph, (x + dx, 32 - dy - glyph.height), glyph)
            x += advance
        bbox = strip.getbbox()
        assert bbox
        visible = strip.crop((0, bbox[1], width, bbox[3]))
        assert visible.height <= 16
        im.paste(visible, ((32 - width) // 2, 32 + (16 - visible.height) // 2))

    draw.rectangle((0, 0, 31, 14), fill=1)
    text(mode, 1, F6, ink=0)
    if mode in ('AUTO', 'MAN'):
        text('1/', 21)
        main_value(('30' if flash else '1000') if mode == 'AUTO' else '125')
        text('s', 48, F6)
    elif mode == 'TIME' and timed_seconds is not None:
        main_value(str(timed_seconds))
        text('s', 48, F6)
    else:
        main_value('B' if mode == 'BULB' else 'T')
    draw.line((5, 61, 26, 61), fill=1)
    if flash:
        draw.rectangle((1, 65, 30, 76), fill=1)
        text('FLASH', 68, ink=0)
    else:
        text('F OFF', 68)
    text('ISO600', 80)
    text({'AUTO': 'EV16.0', 'MAN': 'EV13.0', 'BULB': 'EV5.0', 'TIME': 'EV5.0'}[mode], 95)
    draw.line((2, 107, 29, 107), fill=1)
    text('T--', 111, x=1)
    text('M0', 111, x=21)
    if not wifi and not ble:
        text('AIR', 120)
    else:
        for label, enabled, x in (('W', wifi, 3), ('B', ble, 19)):
            if enabled:
                draw.rectangle((x - 2, 119, x + 10, 127), fill=1)
            text(label + ('+' if enabled else '-'), 120, x=x, ink=0 if enabled else 1)
    return im


def contact_sheet(states, filename, rows):
    sheet = Image.new('RGB', (576, rows * 432), '#222629')
    draw = ImageDraw.Draw(sheet)
    for i, (label, kwargs) in enumerate(states):
        preview = render(**kwargs).resize((96, 384), Image.Resampling.NEAREST)
        x, y = i % 4 * 144 + 24, i // 4 * 432 + 32
        sheet.paste(preview.convert('RGB'), (x, y))
        draw.text((x, y - 22), label, fill='white')
    sheet.save(OUT / filename)


if __name__ == '__main__':
    modes = [(mode + (' F+' if flash else ' F-'), dict(mode=mode, flash=flash))
             for flash in (False, True) for mode in ('AUTO', 'MAN', 'BULB', 'TIME')]
    contact_sheet(modes, 'modes.png', 2)
    radio = [(f'WiFi {int(w)} BLE {int(b)}', dict(wifi=w, ble=b))
             for w, b in ((False, False), (True, False), (False, True), (True, True))]
    contact_sheet(radio, 'radio-states.png', 1)
    portrait = render(wifi=True, ble=True)
    portrait.save(OUT / 'home-32x128.png')
    native = portrait.transpose(Image.Transpose.ROTATE_90)
    native.save(OUT / 'buffer-128x32.png')
    assert native.transpose(Image.Transpose.ROTATE_270).tobytes() == portrait.tobytes()
    render('TIME', timed_seconds=None)
    print('Rendered 8 mode states, 4 radio states and rotation reference; bounds checked.')
