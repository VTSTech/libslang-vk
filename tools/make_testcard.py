#!/usr/bin/env python3
# Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
# AI Assisted by Super-Z (GLM-5.3-Flash)
# License: MIT

"""Generate the shader_demo bundled input image (examples/shader_demo/assets/testcard.png).

A 480x320 test card:
  - band 1 (80px): classic 75% color bars
  - band 2 (80px): horizontal white->black gradient (banding ramp)
  - band 3 (160px): dark blue grid + geometry circles + a row of three
    hand-drawn 32x32 pixel maps (blitted at 3x with an automatic 1px black
    outline pass) and name labels. You can override any character with your
    own art: drop goku.png / mario.png / luigi.png into tools/sprites/
    (transparent background, any size - nearest-neighbor fitted to the slot).
Moving elements are added at runtime by the animated pattern; this static
asset is the "sample image" input source of the demo (loaded through
stb_image, like a LUT).

Usage: python3 tools/make_testcard.py [output.png]
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H = 480, 320

# -- pixel-art sprites (32x32, '.' = transparent) ------------------------------
# Palette letters are shared by all sprites. A 1px black outline is added
# automatically around every sprite by outline_sprite().
PALETTE = {
    "K": (26, 26, 32),      # black: outlines, Goku hair, eyes, pupils
    "H": (78, 78, 94),      # hair highlight
    "S": (255, 208, 152),   # skin
    "T": (226, 172, 120),   # skin shadow (nose)
    "W": (250, 250, 250),   # gloves / eye whites / cap emblem
    "R": (226, 36, 38),     # Mario red / Goku boot trim
    "G": (46, 160, 66),     # Luigi green
    "N": (108, 72, 44),     # brown (hair, mustache, shoes)
    "B": (62, 104, 220),    # denim overalls
    "Y": (252, 204, 60),    # overall buttons
    "O": (247, 132, 30),    # Goku gi orange
    "U": (46, 94, 218),     # Goku undershirt / belt / wristbands blue
    "D": (30, 44, 110),     # Goku boot navy
    "M": (170, 60, 50),     # mouth line
    "P": (114, 60, 176),    # Luigi overalls purple (SMW 1993)
}

# Goku: redrawn after a favpng Dragon Ball sprite reference - jagged black
# hair mass taking ~45% of the sprite (tall top spike, long left spikes,
# zigzag fringe over the forehead), fierce angled brows over narrow eyes,
# wide grin, orange gi with blue V collar, arms flung out with skin fists
# and blue wristbands, blue sash, baggy orange pants, red-trimmed blue boots.
GOKU = [
"................KKKKK...........",
    "...............KKKKKKK..........",
    "..............KKKKKKKK....KKK...",
    "....KKKK.....KKKKKKKKK....KKKKK.",
    "...KKKKK.....KKKKKKKKK....KKKKK.",
    "..KKKKKK......KKKKKKKK....KKKKK.",
    ".KKKKKKKKK....KKKKKKKK....KKKKK.",
    ".KKKKKKKKKK...KKKKKKKKK...KKKKK.",
    ".KKKKKKKKKKK..KKKKKKKKKK..KKKKK.",
    "KKKKKKKKKKKK.KKKKKKKKKKK.KKKKKK.",
    ".KKKHHKKKKKK.KKKKHHKKKKK.KKKKKK.",
    ".KKKKKKKKKKKKKKKKKKKKKKKKKKKKKK.",
    "....KKKKKKKKKKKKKKKKKKKKKKKK....",
    "......SSKKKSSSSKKKSSSSKKKS......",
    ".......SSSSSSSSSSSSSSSSSS.......",
    ".......SKKKKKSSSSSSKKKKKS.......",
    ".......SWWKKWWSSSSWWKKWWS.......",
    ".......SWWKKWSSTTTSSWKKWS.......",
    "........SSSSSMMMMMMSSSSS........",
    "..........SSSSSSSSSSSS..........",
    "......OOOOOOOOSSSSOOOOOOOO......",
    "....OOOOOOOOOUUUUUUOOOOOOOOO....",
    ".SSOOOOOOOOOUUUUUUUUOOOOOOOOOSS.",
    ".SSUUOOOOOOOOOOOOOOOOOOOOOOUUSS.",
    "........UUUUUUUUUUUUUUUU........",
    ".....OOOOOOOOOOOOOOOOOOOOOO.....",
    ".....OOOOOOOOOOOOOOOOOOOOOO.....",
    ".....OOOOOOOOO......OOOOOOOOO...",
    ".....RRRRRRRRR......RRRRRRRRR...",
    ".....UUUUUUUUU......UUUUUUUUU...",
    ".....UUUUUUUUU......UUUUUUUUU...",
    ".....UUUUUUUUU......UUUUUUUUU..."
]

# Luigi: redrawn after the 1993 Super Mario World sprite set (side stance
# facing right): huge head, green cap with white emblem and long brim,
# brown sideburn, big oval eye with the pupil looking right, round nose
# poking out past the face, thin mustache, green shirt, purple overalls
# with yellow buttons, white glove raised in front, brown shoes.
LUIGI = [
    "...........GGGGGGGG.............",
    ".........GGGGGGGGGGGG...........",
    "........GGGGGGGGGGGGGG..........",
    "........GGGWWWGGGGGGGG..........",
    "........GGGWWWWGGGGGGG..........",
    "........GGGGGGGGGGGGGGGGG.......",
    "........GGGGGGGGGGGGGGGGGG......",
    "........NNNSSSSSSSSSSSS.........",
    "........NNNSSSSSSSSSSSS.........",
    "........NNSSSSSSWWWWWSS.........",
    "........NNSSSSSSWWKKWSS.........",
    "........NNSSSSSSWWKKWSS.........",
    "........NNSSSSSSWWKKWSS.........",
    "........NNSSSSSSWWWWWSS.........",
    "........NNNSSSSSSSSSSSSSSS......",
    "........NNNSSSSSSKKKKKKTTTT.....",
    "........NNNSSSSSSMMMMMSSS.......",
    ".........SSSSSSSSSSSSS..........",
    "..........GGGGGGGGGGGG..........",
    ".........GGGGGGGGGGGGGGGGG......",
    ".........GGGPPGGGGPPGGGWWWW.....",
    ".........GGPPPPPPPPPPPWWWWW.....",
    ".........PPPPPPPPPPPPPPWWW......",
    ".........PPYPPPPPPPPYPP.........",
    ".........PPPPPPPPPPPPPP.........",
    "........PPPPPPPPPPPPPPP.........",
    "........PPPPPPP..PPPPPPP........",
    "........PPPPPPP..PPPPPPP........",
    "........PPPPPPP..PPPPPPP........",
    "........NNNNNNN..NNNNNNNNN......",
    "........NNNNNNN..NNNNNNNNN......",
    "........NNNNNNN..NNNNNNNNN......",
]

# Mario: the SMW games build Mario as a straight palette swap of Luigi
# (red cap/shirt, denim overalls instead of purple), so the grid is
# generated from LUIGI with exactly that recolor.
MARIO = [row.replace("G", "R").replace("P", "B") for row in LUIGI]

SPRITE_SIZES = {"GOKU": (32, 32), "MARIO": (32, 32), "LUIGI": (32, 32)}
for _name, _spr in (("GOKU", GOKU), ("MARIO", MARIO), ("LUIGI", LUIGI)):
    _rows, _cols = SPRITE_SIZES[_name]
    assert len(_spr) == _rows, f"{_name}: expected {_rows} rows, got {len(_spr)}"
    for _i, _row in enumerate(_spr):
        assert len(_row) == _cols, \
            f"{_name} row {_i}: expected {_cols} chars, got {len(_row)}"
        assert all(c in PALETTE or c == "." for c in _row), \
            f"{_name} row {_i}: bad char"


def outline_sprite(spr):
    """Pad by 1px on all sides and mark every transparent pixel that touches
    a filled pixel with 'K' -> automatic black outline all around."""
    rows, cols = len(spr), len(spr[0])
    padded = ["." * (cols + 2)] + ["." + r + "." for r in spr] + ["." * (cols + 2)]
    grid = [list(r) for r in padded]
    out = [row[:] for row in grid]
    for y in range(rows + 2):
        for x in range(cols + 2):
            if grid[y][x] != ".":
                continue
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < rows + 2 and 0 <= nx < cols + 2 \
                        and grid[ny][nx] != ".":
                    out[y][x] = "K"
                    break
    return ["".join(r) for r in out]


def blit_sprite(d: ImageDraw.ImageDraw, spr, x0: int, y0: int, scale: int) -> None:
    """Nearest-neighbor blit: every sprite pixel becomes a scale x scale block."""
    for sy, row in enumerate(spr):
        for sx, ch in enumerate(row):
            if ch == ".":
                continue
            x = x0 + sx * scale
            y = y0 + sy * scale
            d.rectangle([x, y, x + scale - 1, y + scale - 1], fill=PALETTE[ch])


def load_user_sprites(sprites_dir: Path) -> dict:
    """Pick up optional user-supplied art: <dir>/goku.png, mario.png, luigi.png.
    Any file found replaces the built-in pixel map for that character."""
    found = {}
    if sprites_dir.is_dir():
        for p in sorted(sprites_dir.glob("*.png")):
            stem = p.stem.strip().lower()
            if stem in ("goku", "mario", "luigi"):
                try:
                    found[stem] = Image.open(p).convert("RGBA")
                except OSError as e:
                    print(f"warning: could not load {p}: {e}")
    return found


def paste_png(img: Image.Image, im: Image.Image, cx: int, base_y: int,
              max_h: int = 100, max_w: int = 112) -> None:
    """Alpha-paste a user PNG into a slot: nearest-neighbor scaled to fit
    (integer factor when upscaling, so pixel art stays crisp), bottom-aligned."""
    w, h = im.size
    ratio = min(max_h / h, max_w / w)
    if ratio >= 1:
        factor = max(1, int(ratio))
        w2, h2 = w * factor, h * factor
    else:
        w2, h2 = max(1, int(w * ratio)), max(1, int(h * ratio))
    im2 = im.resize((w2, h2), Image.NEAREST)
    img.paste(im2, (cx - w2 // 2, base_y - h2), im2)


def main() -> None:
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)

    # -- band 1: classic color bars (75% amplitude) ---------------------------
    bars = [
        (191, 191, 191),  # gray
        (191, 191, 0),    # yellow
        (0, 191, 191),    # cyan
        (0, 191, 0),      # green
        (191, 0, 191),    # magenta
        (191, 0, 0),      # red
        (0, 0, 191),      # blue
    ]
    bar_w = W // len(bars)
    band1_h = 80
    for i, c in enumerate(bars):
        d.rectangle([i * bar_w, 0, (i + 1) * bar_w - 1, band1_h - 1], fill=c)

    # -- band 2: horizontal white->black gradient (banding ramp) --------------
    for x in range(W):
        v = int(255 * x / (W - 1))
        d.line([(x, 80), (x, 159)], fill=(v, v, v))

    # -- band 3: dark blue grid + geometry + pixel-art character row ----------
    bot_y0 = 160
    d.rectangle([0, bot_y0, W - 1, H - 1], fill=(16, 24, 48))
    for gx in range(0, W + 1, 20):
        d.line([(gx, bot_y0), (gx, H - 1)], fill=(60, 80, 120), width=1)
    for gy in range(bot_y0, H + 1, 20):
        d.line([(0, gy), (W - 1, gy)], fill=(60, 80, 120), width=1)

    # Character row: three slots, bottom-aligned, labels underneath.
    # If tools/sprites/<name>.png exists it replaces the built-in pixel map.
    sprites_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else \
        Path(__file__).resolve().parents[1] / "tools" / "sprites"
    user = load_user_sprites(sprites_dir)
    scale = 3
    cell = 34 * scale                       # 102 (32px art + outline border)
    base_y = 272                            # shared sprite bottom line
    row = (
        (GOKU, "goku", 124, "GOKU"),
        (MARIO, "mario", 240, "MARIO"),
        (LUIGI, "luigi", 356, "LUIGI"),
    )
    for spr, key, cx, label in row:
        if key in user:
            paste_png(img, user[key], cx, base_y, max_h=110)
        else:
            outlined = outline_sprite(spr)
            x0 = cx - cell // 2
            y0 = base_y - cell
            blit_sprite(d, outlined, x0, y0, scale)
        try:
            font = ImageFont.load_default()
            tw = d.textlength(label, font=font)
            d.text((cx - tw / 2, base_y + 6), label,
                   fill=(235, 235, 235), font=font)
        except OSError:  # font not available; sprites carry the test anyway
            pass
    if user:
        print("sprites: user pngs ->", ", ".join(f"{k}.png" for k in sorted(user)))
    else:
        print("sprites: built-in pixel maps")

    # Geometry references: one circle each side of the character row.
    for cx in (48, W - 48):
        cy = bot_y0 + 56
        d.ellipse([cx - 22, cy - 22, cx + 22, cy + 22],
                  outline=(240, 240, 240), width=2)
        d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=(230, 60, 60))

    # -- frame + corner markers ------------------------------------------------
    d.rectangle([0, 0, W - 1, H - 1], outline=(255, 255, 255), width=2)
    for cx, cy in ((10, 10), (W - 11, 10), (10, H - 11), (W - 11, H - 11)):
        d.line([(cx - 6, cy), (cx + 6, cy)], fill=(255, 255, 0), width=2)
        d.line([(cx, cy - 6), (cx, cy + 6)], fill=(255, 255, 0), width=2)

    default_out = Path(__file__).resolve().parents[1] / \
        "examples/shader_demo/assets/testcard.png"
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else default_out
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out, "PNG", optimize=True)
    print("wrote", out, img.size)


if __name__ == "__main__":
    main()
