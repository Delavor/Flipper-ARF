#!/usr/bin/env python3
"""
extract_doom_assets.py

Reads the user's own Doom shareware IWAD (Doom1.WAAD is freely distributable
as a whole) and converts a selection of its sprites into 1-bit assets in the
FlipperCatacombs engine formats. Nothing from the WAD is stored in this
repository: the header is generated locally at build time from the WAD the
user already has.

Usage:
    python3 tools/extract_doom_assets.py <path/to/Doom1.WAD> [output_header]

Output (default): game/Generated/DoomSprites.inc.h

Engine formats
--------------
1) Scaled sprite (16x16), uint16_t array, per frame:
   16 columns x 2 words: [transparency mask, colour], bit v = row v (bit0=top)
2) Page sprite (Platform::DrawSprite): uint8_t array:
   w, h, then per page (8 rows), per column: [colour byte, mask byte]
   (bit0 = top row of the page)
3) Solid bitmap (Platform::DrawSolidBitmap): uint8_t array:
   w, h, then per page, per column: colour byte where bit=1 means BLACK
   (DrawSolidBitmap writes fill = ~src)
4) HUD icon: 8 raw page bytes (bit=1 -> white pixel)
"""

import struct
import sys
import os

# ---------------------------------------------------------------- WAD parsing


class Wad:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        ident, numlumps, diroff = struct.unpack_from("<4sII", self.data, 0)
        if ident not in (b"IWAD", b"PWAD"):
            raise ValueError("Not a WAD file")
        self.lumps = {}
        for i in range(numlumps):
            off, size, name = struct.unpack_from("<II8s", self.data, diroff + 16 * i)
            name = name.rstrip(b"\0").decode("ascii", "replace")
            # keep first occurrence (IWAD order)
            if name not in self.lumps:
                self.lumps[name] = (off, size)

    def lump(self, name):
        off, size = self.lumps[name]
        return self.data[off : off + size]

    def has(self, name):
        return name in self.lumps


def load_palette(wad):
    pal = wad.lump("PLAYPAL")[:768]
    grays = []
    for i in range(256):
        r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
        grays.append(0.299 * r + 0.587 * g + 0.114 * b)
    return grays


def decode_picture(wad, name, grays):
    """Decode Doom picture format -> (w, h, pixels) where pixels is a list of
    rows; each entry is None (transparent) or gray 0..255."""
    raw = wad.lump(name)
    w, h, _lo, _to = struct.unpack_from("<hhhh", raw, 0)
    colofs = struct.unpack_from("<%di" % w, raw, 8)
    pix = [[None] * w for _ in range(h)]
    for x in range(w):
        p = colofs[x]
        while raw[p] != 0xFF:
            topdelta = raw[p]
            length = raw[p + 1]
            p += 3  # topdelta, length, pad
            for i in range(length):
                y = topdelta + i
                if 0 <= y < h:
                    pix[y][x] = grays[raw[p]]
                p += 1
            p += 1  # trailing pad
    return w, h, pix


# ------------------------------------------------------------- image helpers

BAYER4 = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
]


def bbox(pix):
    xs, ys = [], []
    for y, row in enumerate(pix):
        for x, v in enumerate(row):
            if v is not None:
                xs.append(x)
                ys.append(y)
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def crop(pix, x0, y0, x1, y1):
    return [row[x0:x1] for row in pix[y0:y1]]


def box_resize(pix, nw, nh):
    """Box-filter resize of (gray|None) grid; alpha = coverage."""
    h = len(pix)
    w = len(pix[0])
    out_gray = [[0.0] * nw for _ in range(nh)]
    out_alpha = [[0.0] * nw for _ in range(nh)]
    for ny in range(nh):
        sy0 = ny * h / nh
        sy1 = (ny + 1) * h / nh
        for nx in range(nw):
            sx0 = nx * w / nw
            sx1 = (nx + 1) * w / nw
            acc_g = acc_a = acc_w = 0.0
            y = int(sy0)
            while y < sy1 and y < h:
                wy = min(sy1, y + 1) - max(sy0, y)
                x = int(sx0)
                while x < sx1 and x < w:
                    wx = min(sx1, x + 1) - max(sx0, x)
                    weight = wx * wy
                    acc_w += weight
                    v = pix[y][x]
                    if v is not None:
                        acc_a += weight
                        acc_g += weight * v
                    x += 1
                y += 1
            if acc_w > 0 and acc_a > 0:
                out_gray[ny][nx] = acc_g / acc_a
                out_alpha[ny][nx] = acc_a / acc_w
    return out_gray, out_alpha


def normalize(gray, alpha, thresh=0.5):
    """Per-sprite contrast stretch over opaque pixels."""
    vals = [
        gray[y][x]
        for y in range(len(gray))
        for x in range(len(gray[0]))
        if alpha[y][x] >= thresh
    ]
    if not vals:
        return gray
    lo, hi = min(vals), max(vals)
    if hi - lo < 1e-6:
        hi = lo + 1.0
    return [
        [(v - lo) * 255.0 / (hi - lo) for v in row]
        for row in gray
    ]


def to_1bit(gray, alpha, bias=0.0):
    """3-tone quantization (black / 50% checker / white) for clean tiny
    sprites. Returns (colour, mask) row-major bools."""
    h, w = len(gray), len(gray[0])
    colour = [[False] * w for _ in range(h)]
    mask = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if alpha[y][x] >= 0.5:
                mask[y][x] = True
                v = gray[y][x] + bias
                if v < 80:
                    colour[y][x] = False
                elif v < 175:
                    colour[y][x] = ((x + y) & 1) == 0
                else:
                    colour[y][x] = True
    return colour, mask


def fit_grid(pix, size, valign, hpad=0):
    """Crop to bbox, keep aspect, fit into size x size grid.
    valign: 'bottom' or 'center'."""
    x0, y0, x1, y1 = bbox(pix)
    pix = crop(pix, x0, y0, x1, y1)
    w = x1 - x0
    h = y1 - y0
    avail = size - hpad * 2
    if w >= h:
        nw = avail
        nh = max(1, round(h * avail / w))
    else:
        nh = avail
        nw = max(1, round(w * avail / h))
    gray, alpha = box_resize(pix, nw, nh)
    # paste into size x size
    g = [[0.0] * size for _ in range(size)]
    a = [[0.0] * size for _ in range(size)]
    ox = (size - nw) // 2
    oy = (size - nh) if valign == "bottom" else (size - nh) // 2
    for y in range(nh):
        for x in range(nw):
            g[oy + y][ox + x] = gray[y][x]
            a[oy + y][ox + x] = alpha[y][x]
    return g, a


# ------------------------------------------------------------ format emitters


def emit_scaled16(frames):
    """frames: list of (colour, mask) 16x16 row-major -> list of uint16 words."""
    words = []
    for colour, mask in frames:
        for x in range(16):
            t = c = 0
            for y in range(16):
                if mask[y][x]:
                    t |= 1 << y
                    if colour[y][x]:
                        c |= 1 << y
            words.append(t)
            words.append(c)
    return words


def emit_page_sprite(colour, mask):
    """-> list of bytes: w, h, then per page per column [colour, mask]."""
    h, w = len(colour), len(colour[0])
    pages = (h + 7) // 8
    out = [w, h]
    for page in range(pages):
        for x in range(w):
            cb = mb = 0
            for bit in range(8):
                y = page * 8 + bit
                if y < h and mask[y][x]:
                    mb |= 1 << bit
                    if colour[y][x]:
                        cb |= 1 << bit
            out.append(cb)
            out.append(mb)
    return out


def emit_solid_bitmap(colour):
    """DrawSolidBitmap: bit=1 -> black. colour True = white pixel."""
    h, w = len(colour), len(colour[0])
    pages = (h + 7) // 8
    out = [w, h]
    for page in range(pages):
        for x in range(w):
            b = 0
            for bit in range(8):
                y = page * 8 + bit
                if y < h and not colour[y][x]:
                    b |= 1 << bit
            out.append(b)
    return out


def fmt_words(words, per_line=16):
    lines = []
    for i in range(0, len(words), per_line):
        lines.append(",".join("0x%x" % v for v in words[i : i + per_line]))
    return ",\n\t".join(lines)


# ------------------------------------------------------------------ pipeline


def sprite16(wad, grays, names, valign, bias=0.0):
    frames = []
    for n in names:
        w, h, pix = decode_picture(wad, n, grays)
        g, a = fit_grid(pix, 16, valign)
        g = normalize(g, a)
        frames.append(to_1bit(g, a, bias))
    return emit_scaled16(frames)


def first_present(wad, *names):
    for n in names:
        if wad.has(n):
            return n
    raise KeyError("none of %s in WAD" % (names,))


def build_weapon(wad, grays, target_w=46):
    """Idle shotgun + firing frame (shotgun with muzzle flash composited)."""
    w, h, gun = decode_picture(wad, "SHTGA0", grays)
    x0, y0, x1, y1 = bbox(gun)
    gun = crop(gun, x0, y0, x1, y1)
    gw, gh = x1 - x0, y1 - y0
    nw = target_w
    nh = max(1, round(gh * nw / gw))
    g, a = box_resize(gun, nw, nh)
    g = normalize(g, a)
    # limit height so it doesn't cover too much of the 64px screen: keep the
    # top rows (barrel); the grip sticks out of the screen bottom like in Doom
    max_h = 28
    if nh > max_h:
        g = g[:max_h]
        a = a[:max_h]
        nh = max_h
    idle = to_1bit(g, a)

    # firing frame: muzzle flash above the barrel
    fname = first_present(wad, "SHTFB0", "SHTFA0")
    fw, fh, fl = decode_picture(wad, fname, grays)
    fx0, fy0, fx1, fy1 = bbox(fl)
    fl = crop(fl, fx0, fy0, fx1, fy1)
    fsw = max(1, round((fx1 - fx0) * nw / gw))
    fsh = max(1, round((fy1 - fy0) * nw / gw))
    fg, fa = box_resize(fl, fsw, fsh)
    fg = normalize(fg, fa)
    # keep total height reasonable: crop the top of the flash if needed
    max_total = 38
    if nh + fsh > max_total:
        cut = nh + fsh - max_total
        fg = fg[cut:]
        fa = fa[cut:]
        fsh -= cut

    # find barrel top-center of scaled gun: centroid of top opaque row
    top_row = 0
    for y in range(nh):
        if any(a[y][x] >= 0.5 for x in range(nw)):
            top_row = y
            break
    cols = [x for x in range(nw) if a[top_row][x] >= 0.5]
    cx = sum(cols) // len(cols) if cols else nw // 2

    fire_h = nh + fsh
    FG = [[0.0] * nw for _ in range(fire_h)]
    FA = [[0.0] * nw for _ in range(fire_h)]
    for y in range(nh):
        for x in range(nw):
            FG[fsh + y][x] = g[y][x]
            FA[fsh + y][x] = a[y][x]
    ox = cx - fsw // 2
    for y in range(fsh):
        for x in range(fsw):
            dx = ox + x
            if 0 <= dx < nw and fa[y][x] >= 0.5:
                FG[y][dx] = fg[y][x]
                FA[y][dx] = fa[y][x]
    fire = to_1bit(FG, FA, bias=40.0)  # flash reads brighter

    return emit_page_sprite(*idle), emit_page_sprite(*fire)


TITLE_LETTERS = {
    "D": [
        "######.",
        "##..##.",
        "##..###",
        "##...##",
        "##...##",
        "##..###",
        "##..##.",
        "######.",
    ],
    "O": [
        ".#####.",
        "##...##",
        "##...##",
        "##...##",
        "##...##",
        "##...##",
        "##...##",
        ".#####.",
    ],
    "M": [
        "##...##",
        "###.###",
        "#######",
        "##.#.##",
        "##...##",
        "##...##",
        "##...##",
        "##...##",
    ],
}


def build_title(wad, grays):
    """Original blocky 'DOOM' pixel title with dithered gradient, 128x64."""
    W, H = 128, 64
    colour = [[False] * W for _ in range(H)]
    scale = 4  # each letter 7x8 -> 28x32
    lw, lh = 7 * scale, 8 * scale
    gap = 4
    total = 4 * lw + 3 * gap
    x0 = (W - total) // 2
    y0 = (H - lh) // 2
    for i, ch in enumerate("DOOM"):
        gl = TITLE_LETTERS[ch]
        ox = x0 + i * (lw + gap)
        for gy in range(8):
            for gx in range(7):
                if gl[gy][gx] != "#":
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        x = ox + gx * scale + sx
                        y = y0 + gy * scale + sy
                        # metallic gradient: solid on top, dithered below
                        if y - y0 < lh * 5 // 8:
                            colour[y][x] = True
                        else:
                            colour[y][x] = ((x + y) & 1) == 0
    return emit_solid_bitmap(colour)


def icon_from_pixmap(rows):
    """8x8 pixmap ('#'=white) -> 8 page bytes, bit0 = top row."""
    out = []
    for x in range(8):
        b = 0
        for y in range(8):
            if rows[y][x] == "#":
                b |= 1 << y
        out.append(b)
    return out


HEALTH_ICON = [
    "..###...",
    "..#.#...",
    "###.###.",
    "#.....#.",
    "###.###.",
    "..#.#...",
    "..###...",
    "........",
]

AMMO_ICON = [
    ".#..#...",
    "###.###.",
    "###.###.",
    "###.###.",
    "###.###.",
    "###.###.",
    "###.###.",
    "........",
]


def main():
    wad_path = sys.argv[1] if len(sys.argv) > 1 else "../doomgeneric/Doom1.WAD"
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = (
        sys.argv[2]
        if len(sys.argv) > 2
        else os.path.join(root, "game", "Generated", "DoomSprites.inc.h")
    )

    wad = Wad(wad_path)
    grays = load_palette(wad)

    scaled = []  # (symbol, numFrames, words)

    def add16(symbol, names, valign, bias=0.0):
        scaled.append((symbol, len(names), sprite16(wad, grays, names, valign, bias)))

    # enemies (2 walk frames each)
    add16("skeletonSpriteData", ["SARGA1", "SARGB1"], "bottom")  # pinky demon
    add16("mageSpriteData", ["TROOA1", "TROOB1"], "bottom")      # imp
    add16("batSpriteData", ["SPOSA1", "SPOSB1"], "bottom")       # shotgun sergeant
    add16("spiderSpriteData", ["POSSA1", "POSSB1"], "bottom")    # zombieman

    # projectiles
    ball = first_present(wad, "BAL1A0")
    ball2 = first_present(wad, "BAL1B0", "BAL1A0")
    add16("projectileSpriteData", [ball], "center", bias=60.0)
    add16("enemyProjectileSpriteData", [ball2], "center", bias=60.0)

    # decorations / pickups
    torch1 = first_present(wad, "TREDA0", "CANDA0")
    torch2 = first_present(wad, "TREDC0", "TREDB0", "CANDA0")
    add16("torchSpriteData1", [torch1], "center", bias=40.0)
    add16("torchSpriteData2", [torch2], "center", bias=40.0)
    add16("urnSpriteData", ["BAR1A0"], "bottom")                 # barrel
    add16("potionSpriteData", ["STIMA0"], "bottom", bias=30.0)   # stimpack
    add16("chestSpriteData", ["BPAKA0"], "bottom", bias=30.0)    # backpack
    add16("chestOpenSpriteData", ["CLIPA0"], "bottom", bias=30.0)
    add16("scrollSpriteData", ["BON2A0"], "bottom", bias=30.0)   # armor helmet
    add16("coinsSpriteData", ["BON1A0"], "bottom", bias=30.0)    # potion bottle
    add16("crownSpriteData", ["ARM1A0"], "bottom", bias=30.0)    # green armor
    add16("signSpriteData", ["POL5A0"], "bottom", bias=20.0)     # skull pile

    weapon_idle, weapon_fire = build_weapon(wad, grays)
    title = build_title(wad, grays)
    health = icon_from_pixmap(HEALTH_ICON)
    ammo = icon_from_pixmap(AMMO_ICON)

    with open(out_path, "w") as f:
        f.write("// Auto-generated by tools/extract_doom_assets.py\n")
        f.write("// Derived at build time from the user's local Doom shareware WAD.\n")
        f.write("// Do not commit WAD-derived data to public repositories.\n\n")
        for symbol, nframes, words in scaled:
            f.write("constexpr uint8_t %s_numFrames = %d;\n" % (symbol, nframes))
            f.write("extern const uint16_t %s[] PROGMEM =\n{\n\t%s\n};\n" % (symbol, fmt_words(words)))
        f.write("extern const uint8_t handSpriteData1[] PROGMEM =\n{\n\t%s\n};\n" % fmt_words(weapon_idle, 24))
        f.write("extern const uint8_t handSpriteData2[] PROGMEM =\n{\n\t%s\n};\n" % fmt_words(weapon_fire, 24))
        f.write("extern const uint8_t titleBitmapData[] PROGMEM =\n{\n\t%s\n};\n" % fmt_words(title, 24))
        f.write("extern const uint8_t heartSpriteData[] PROGMEM =\n{\n%s\n};\n" % fmt_words(health))
        f.write("extern const uint8_t manaSpriteData[] PROGMEM =\n{\n%s\n};\n" % fmt_words(ammo))

    total = sum(len(w) * 2 for _, _, w in scaled) + len(weapon_idle) + len(weapon_fire) + len(title) + 16
    print("Wrote %s (%d bytes of asset data)" % (out_path, total))


if __name__ == "__main__":
    main()
