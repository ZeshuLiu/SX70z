"""Compile and exercise the firmware's actual drawing code with a host C compiler.

Set CC to a GCC-compatible compiler if gcc/clang is not on PATH. Generated
files go in a temporary directory. This does not emulate the ESP LCD backend.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
HERE = Path(__file__).resolve().parent
cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang") or shutil.which("cl")
if not cc:
    raise SystemExit("Host GCC or Clang required; set CC to its executable path.")

# Extract unmodified drawing functions while omitting ESP-IDF panel init and
# I/O; the panel draw call is a validating stub. No firmware file is modified.
driver = (SRC / "ssd1306.c").read_text(encoding="utf-8")
start = driver.index("static void draw_pixel(")
init = driver.index("bool ssd1306_init(")
clear = driver.index("void ssd1306_clear(")
invert = driver.index("void ssd1306_invert(")
show = driver.index("void ssd1306_show(")
contrast = driver.index("void ssd1306_contrast(")
body = (driver[start:init] + driver[clear:invert] +
        driver[show:contrast] + driver[contrast:])
with tempfile.TemporaryDirectory(prefix="sx70-ui-host-") as tmp:
    tmp = Path(tmp)
    driver_file = tmp / "driver.c"
    driver_file.write_text('#include <stdlib.h>\n#include <string.h>\n#include "ssd1306.h"\n'
                           'void esp_lcd_panel_draw_bitmap(void *, int, int, int, int, const void *);\n' + body,
                           encoding="utf-8")
    exe = tmp / ("ui_host.exe" if os.name == "nt" else "ui_host")
    files = [HERE / "test_ui.c", driver_file, SRC / "display_manager.c",
             *[SRC / "fonts" / f for f in ("font5x8.c", "font6x12.c", "font_terminus8x14.c")]]
    if Path(cc).stem.lower() == "cl":
        command = [str(cc), "/nologo", "/utf-8", "/std:c11", "/W3", "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(HERE / "include"),
                   "/I" + str(SRC), *map(str, files), "/Fe:" + str(exe)]
    else:
        command = [str(cc), "-std=c11", "-O1", "-Wall", "-Wextra", "-I", str(HERE / "include"),
                   "-I", str(SRC), *map(str, files), "-lm", "-o", str(exe)]
    subprocess.run(command, check=True, cwd=tmp)
    output = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
    if output:
        output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(exe), *([str(output)] if output else [])], check=True)
    if output:
        from PIL import Image, ImageDraw
        files = sorted(output.glob("*.pbm"))
        scale, columns = 3, 8
        tile_w, tile_h = 32 * scale, 128 * scale + 22
        sheet = Image.new("RGB", (columns * tile_w, ((len(files) + columns - 1) // columns) * tile_h), "#303030")
        draw = ImageDraw.Draw(sheet)
        for i, path in enumerate(files):
            x, y = (i % columns) * tile_w, (i // columns) * tile_h
            # PBM black pixels represent lit OLED pixels: invert for a black OLED background.
            from PIL import ImageOps
            pic = ImageOps.invert(Image.open(path).convert("L")).resize((32 * scale, 128 * scale), Image.Resampling.NEAREST)
            sheet.paste(pic.convert("RGB"), (x, y + 20))
            draw.text((x + 1, y + 2), path.stem, fill="white")
        sheet.save(output / "contact-sheet.png")
        print("Wrote", output / "contact-sheet.png")
