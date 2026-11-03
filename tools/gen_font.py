"""Generuje bitmapowe czcionki LVGL (lv_font_fmt_txt, 4 bpp) z polskimi znakami.

Źródło: Montserrat-Medium.ttf z paczki LVGL (scripts/built_in_font/), licencja SIL OFL 1.1.
Rasteryzacja przez Pillow – zamiennik lv_font_conv (Node.js) dla tego projektu.
Bitmapy są gotowe w pamięci flash, więc rysowanie nie rasteryzuje glifów w locie
(Tiny TTF był ~8x wolniejszy przy budowie i rysowaniu strony).

Użycie: tools/.venv/bin/python tools/gen_font.py <ścieżka do Montserrat-Medium.ttf>
Wynik:  lib/app_display/src/font_pl_<rozmiar>.c dla rozmiarów z SIZES.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "lib" / "app_display" / "src"
SIZES = (13, 14, 16)

ASCII = list(range(0x20, 0x7F))
EXTRA = sorted(ord(c) for c in "ąćęłńóśźżĄĆĘŁŃÓŚŹŻ…–—°")


def render_glyph(font: ImageFont.FreeTypeFont, cp: int):
    ch = chr(cp)
    adv = font.getlength(ch)
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")  # względem punktu bazowego (y w dół)
    w, h = max(0, x1 - x0), max(0, y1 - y0)
    pixels = []
    if w and h:
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        pixels = list(img.tobytes())
    # 4 bpp, wiersze ciągiem, starszy półbajt pierwszy
    nibbles = [(p + 8) // 17 for p in pixels]
    if len(nibbles) % 2:
        nibbles.append(0)
    data = bytes((nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2))
    return {
        "data": data, "adv_w": round(adv * 16), "box_w": w, "box_h": h,
        "ofs_x": x0, "ofs_y": -y1,  # od linii bazowej do dolnej krawędzi bitmapy (w górę dodatnio)
    }


def gen(ttf: str, size: int) -> str:
    font = ImageFont.truetype(ttf, size)
    ascent, descent = font.getmetrics()
    codepoints = ASCII + EXTRA
    glyphs = [render_glyph(font, cp) for cp in codepoints]

    bitmap, dsc = bytearray(), ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}, /* id 0 */"]
    for cp, g in zip(codepoints, glyphs):
        dsc.append(f"    {{.bitmap_index = {len(bitmap)}, .adv_w = {g['adv_w']}, .box_w = {g['box_w']}, "
                   f".box_h = {g['box_h']}, .ofs_x = {g['ofs_x']}, .ofs_y = {g['ofs_y']}}}, /* U+{cp:04X} */")
        bitmap += g["data"]

    name = f"font_pl_{size}"
    extra_start = EXTRA[0]
    extra_ofs = ", ".join(str(cp - extra_start) for cp in EXTRA)
    bitmap_rows = [", ".join(f"0x{b:02X}" for b in bitmap[i:i + 16]) for i in range(0, len(bitmap), 16)]

    return f"""/*
 * Montserrat Medium {size} px, 4 bpp: ASCII + polskie znaki. Wygenerowane przez tools/gen_font.py – nie edytować.
 * Copyright 2011 The Montserrat Project Authors. Licencja SIL Open Font License 1.1 (https://openfontlicense.org).
 */
#include "lvgl.h"

static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {{
    {(","+chr(10)+"    ").join(bitmap_rows)}
}};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
{chr(10).join(dsc)}
}};

static const uint16_t extra_ofs[] = {{ {extra_ofs} }};

static const lv_font_fmt_txt_cmap_t cmaps[] = {{
    {{.range_start = 0x20, .range_length = {len(ASCII)}, .glyph_id_start = 1,
     .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}},
    {{.range_start = 0x{extra_start:04X}, .range_length = {EXTRA[-1] - extra_start + 1}, .glyph_id_start = {len(ASCII) + 1},
     .unicode_list = extra_ofs, .glyph_id_ofs_list = NULL, .list_length = {len(EXTRA)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}},
}};

static const lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 2,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
}};

const lv_font_t {name} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {ascent + descent},
    .base_line = {descent},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &font_dsc,
    .fallback = &lv_font_montserrat_14, /* symbole LV_SYMBOL_* */
    .user_data = NULL,
}};
"""


def main(ttf: str) -> None:
    for size in SIZES:
        out = OUT_DIR / f"font_pl_{size}.c"
        out.write_text(gen(ttf, size))
        print(f"Zapisano {out.relative_to(ROOT)}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
