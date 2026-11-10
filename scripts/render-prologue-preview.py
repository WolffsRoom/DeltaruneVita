#!/usr/bin/env python3
"""Render the Chapter 0 prologue (VitaGL egg intro) on the PC, stage by stage.

Mirrors src/vita-probe/source/vita_prologue_egg_gl.inc and
vita_prologue_text_gl.inc: same glyph atlas and metrics, same strings, same
word wrap and placement. Assets come from the VPK (prologue.bin, glyph atlas,
cross icon). Also reports text that leaves the dialogue box.

usage: render-prologue-preview.py --vpk <file.vpk> --out <dir> [--langs 0,8]
"""
import argparse, io, math, re, struct, zipfile
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src/vita-probe/source"

def parse_c_string(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"n": "\n", "\\": "\\", '"': '"', "t": "\t"}.get(n, n)); i += 2
        else:
            out.append(c); i += 1
    return "".join(out)

def load_tables():
    text = (SRC / "vita_prologue_glyphs.h").read_text(encoding="utf-8")
    glyphs = {}
    for m in re.finditer(r"\{(\d+),(\d+),(\d+),(\d+),(\d+),(-?\d+),(-?\d+),(\d+),(\d+)\}", text):
        v = list(map(int, m.groups()))
        glyphs[v[0]] = dict(x=v[1], y=v[2], w=v[3], h=v[4], bx=v[5], by=v[6], adv=v[7], kind=v[8])
    def block(name):
        i = text.index(name); j = text.index("};", i)
        return [parse_c_string(s) for s in re.findall(r'"((?:[^"\\]|\\.)*)"', text[i:j])]
    strings = block("egg_strings[10][11]")
    strings = [strings[k * 11:(k + 1) * 11] for k in range(10)]
    headings = block("egg_language_headings")
    near = block("egg_near_prompt")
    display = block("egg_language_display")
    choices = block("egg_choice_labels")
    choices = [choices[k * 2:k * 2 + 2] for k in range(10)]
    return glyphs, strings, headings, display, choices, near

class Pack:
    def __init__(self, data):
        magic, count, start = struct.unpack_from("<4sII", data, 0)
        assert magic == b"VAP1"
        self.files = {}
        for i in range(count):
            name, off, size, crc, _ = struct.unpack_from("<144sIIII", data, 12 + 160 * i)
            self.files[name.partition(b"\0")[0].decode()] = data[off:off + size]
    def image(self, path):
        return Image.open(io.BytesIO(self.files[path])).convert("RGBA")

LANGS = ["English", "Portuguese-BR", "Japanese", "Spanish", "French", "German",
         "Italian", "Turkish", "Russian", "Russkiy"]
SLOTS = [0, 3, 4, 1, 7, 2, 8, 5, 6, 6, 10]

class Renderer:
    def __init__(self, vpk, fit):
        z = zipfile.ZipFile(vpk)
        self.pack = Pack(z.read("assets/prologue.bin"))
        for n in z.namelist():  # loose app0:assets/prologue/ files (egg_asset_read fallback)
            if n.startswith("assets/prologue/") and n.endswith(".png"):
                self.pack.files.setdefault(n[len("assets/prologue/"):], z.read(n))
        self.atlas = Image.open(io.BytesIO(z.read("assets/prologue_font_glyphs.png"))).convert("RGBA")
        self.cross = Image.open(io.BytesIO(z.read("assets/prologue_cross_ps4.png"))).convert("RGBA")
        self.glyphs, self.strings, self.headings, self.display, self.choices, self.near = load_tables()
        self.fit = fit

    def glyph(self, cp):
        return self.glyphs.get(cp) or self.glyphs.get(ord("?"))

    # --- egg_live_draw -------------------------------------------------
    def layout(self, s, x, y, max_w, scale, cross_icon, base_kind_shift=None):
        if base_kind_shift is None: base_kind_shift = self.fit
        quads, pen_x, pen_y = [], x, y
        i = 0
        def is_cross(k):
            if not cross_icon or s[k] != "X": return False
            prev_ok = k == 0 or s[k - 1] in " /("
            nxt = s[k + 1] if k + 1 < len(s) else ""
            return prev_ok and (nxt == "" or nxt in " .,")
        while i < len(s):
            ch = s[i]; cp = ord(ch)
            if ch == "\n":
                pen_x = x; pen_y += 37.0 * scale; i += 1; continue
            cross = is_cross(i)
            g = self.glyph(cp)
            adv = (27 if cross else (g["adv"] if g else 14)) * scale
            if ch == " ":
                nw, k = 0.0, i + 1
                while k < len(s) and s[k] not in " \n":
                    ng = self.glyph(ord(s[k]))
                    nw += (27 if is_cross(k) else (ng["adv"] if ng else 14)) * scale; k += 1
                if pen_x > x and pen_x + adv + nw > x + max_w:
                    pen_x = x; pen_y += 37.0 * scale; i += 1; continue
            if pen_x > x and pen_x + adv > x + max_w:
                pen_x = x; pen_y += 37.0 * scale
            if cross:
                quads.append(("cross", pen_x, pen_y + 5 * scale, 22, 26))
            elif g and g["w"] and g["h"]:
                dy = self.kind_shift(g) * scale if base_kind_shift else 0
                quads.append((g, pen_x + g["bx"] * scale, pen_y + g["by"] * scale + dy,
                              g["w"] * scale, g["h"] * scale))
            pen_x += adv; i += 1
        self.last_bottom = pen_y + 29.0 * scale
        return quads

    @staticmethod
    def kind_shift(g):
        return 17 if g["kind"] == 2 else (13 if g["kind"] == 1 else 0)

    def draw_quads(self, img, quads, color, alpha=1.0):
        for q in quads:
            if q[0] == "cross":
                _, x, y, w, h = q
                spr = self.cross.resize((w, h), Image.NEAREST)
                img.alpha_composite(spr, (int(round(x)), int(round(y))))
                continue
            g, x, y, w, h = q
            crop = self.atlas.crop((g["x"], g["y"], g["x"] + g["w"], g["y"] + g["h"]))
            if (w, h) != (g["w"], g["h"]):
                crop = crop.resize((max(1, int(round(w))), max(1, int(round(h)))), Image.BILINEAR)
            r, gg, b = color
            tint = Image.new("RGBA", crop.size, (r, gg, b, 255))
            a = crop.getchannel("A").point(lambda v: int(v * alpha))
            tint.putalpha(a)
            img.alpha_composite(tint, (int(round(x)), int(round(y))))

    @staticmethod
    def bbox(quads):
        if not quads: return None
        return (min(q[1] for q in quads), min(q[2] for q in quads),
                max(q[1] + q[3] for q in quads), max(q[2] + q[4] for q in quads))

    # --- egg_draw_text (dialogue) ---------------------------------------
    def dialog_quads(self, lang, slot):
        s = self.strings[lang][slot]
        if self.fit:
            return self.fit_layout(s)
        scale = 0.70 if lang == 2 else (0.75 if lang in (8, 9) else 1.0)
        yoff = 6 if lang in (2, 8, 9) else 14
        return self.layout(s, 52 + 9, 364 + yoff, 838, scale, True)

    # v0.82 (vita_prologue_egg_gl.inc egg_draw_text): largest scale that fits.
    def fit_layout(self, s):
        scale = 1.0
        while scale > 0.56:
            self.layout(s, 61, 378, 838, scale, True)
            if self.last_bottom <= 513: break
            scale -= 0.04
        return self.layout(s, 61, 378, 838, scale, True)

    def width(self, s):
        return sum((self.glyph(ord(c)) or {"adv": 14})["adv"] for c in s)

    def scene(self, lang, stage, frame=200, selected_yes=True, near=False):
        img = Image.new("RGBA", (960, 544), (0, 0, 0, 255))
        P = self.pack
        world = Image.new("RGBA", (960, 544), (0, 0, 0, 0))
        def wimg(path, x, y, w, h, alpha=1.0):
            im = P.image(path).resize((int(w), int(h)), Image.NEAREST)
            if alpha < 1: im.putalpha(im.getchannel("A").point(lambda v: int(v * alpha)))
            world.alpha_composite(im, (int(160 + x), int(32 + y)))
        wimg("ch1_original/room_man_tiles.png", 0, 0, 640, 480)
        siner = frame + 235.0
        wimg("ch1_original/spr_blocktree_parts_1.png", 240, 80, 210, 182)
        wimg("ch1_original/spr_blocktree_parts_2.png", 240 + math.sin(siner / 12) * 2, 80 + math.cos(siner / 20) * 2, 210, 182)
        wimg("ch1_original/spr_blocktree_parts_3.png", 240 + math.sin(siner / 14), 80 + math.cos(siner / 24), 210, 182)
        py = 280 if (stage != 0 or near) else 360
        wimg("ch1_original/spr_krisu_dark_0.png", 300, py, 38, 76)
        img.alpha_composite(world.crop((160, 32, 800, 512)), (160, 32))
        d = ImageDraw.Draw(img, "RGBA")
        report = []
        if stage == 0:
            s = self.near[lang] if near else self.strings[lang][0]
            w = self.width(s) + (8 if (near and "X" in s) else 0)
            q = self.layout(s, (960 - w) * 0.5, 455, 820, 1.0, True)
            self.draw_quads(img, q, (255, 255, 255)); report.append(("prompt", self.bbox(q), (0, 0, 960, 544)))
        else:
            img.alpha_composite(P.image("dialogbox.png").resize((908, 178), Image.NEAREST), (26, 351))
            slot = SLOTS[stage]
            q = self.dialog_quads(lang, slot)
            self.draw_quads(img, q, (255, 255, 255))
            report.append(("dialog", self.bbox(q), (26 + 14, 351 + 12, 934 - 14, 529 - 12)))
            if stage == 1:
                shade = Image.new("RGBA", (438, 192) if self.fit else (438, 182), (0, 0, 0, 196 if self.fit else 116)); img.alpha_composite(shade, (261, 17))
                icon = "icons/%s.png" % LANGS[lang]
                if icon in P.files:
                    img.alpha_composite(P.image(icon).resize((70, 58), Image.NEAREST), (445, 80))
                bounce = abs(math.sin(frame * 0.06)) * 4.0
                for text, ty, mw, col in ((self.headings[lang], 39, 440, (255, 255, 255)), (self.display[lang], 156 - bounce, 450, (255, 225, 63))):
                    w = self.width(text)
                    q = self.layout(text, (960 - w) * 0.5, ty, mw, 1.0, False)
                    self.draw_quads(img, q, col)
                if self.fit:
                    avail = [k for k in (0, 2, 1, 3, 4, 5, 6, 7, 8)]
                    pos = avail.index(lang) if lang in avail else 0
                    dx = (960 - (len(avail) * 14 - 6)) / 2
                    for k in range(len(avail)):
                        c = (255, 225, 63, 245) if k == pos else (110, 110, 130, 200)
                        d.rectangle((dx + k * 14, 190, dx + k * 14 + 7, 197), fill=c)
                left, right = [4, 2, 1, 2, 4], [1, 2, 4, 2, 1]
                for row in range(5):
                    for col in range(3):
                        if left[row] & (1 << col): d.rectangle((273 + col * 4, 100 + row * 4, 276 + col * 4, 103 + row * 4), fill=(255, 225, 63, 245))
                        if right[row] & (1 << col): d.rectangle((680 + col * 4, 100 + row * 4, 683 + col * 4, 103 + row * 4), fill=(255, 225, 63, 245))
            if stage == 7:
                for opt, ox in ((0, 258), (1, 588)):
                    sel = selected_yes if opt == 0 else not selected_yes
                    q = self.layout(self.choices[lang][opt], ox + 24, 463 + 5, 200, 1.0, False)
                    self.draw_quads(img, q, (255, 228, 56) if sel else (255, 255, 255))
                    report.append(("option%d" % opt, self.bbox(q), (26 + 14, 351 + 12, 934 - 14, 529 - 12)))
        return img.convert("RGB"), report

STAGE_NAMES = ["0_arvore", "1_idioma", "2_idioma_confirmado", "3_obrigado", "4_projeto", "5_trofeus",
               "6_capunlocker", "7_escolha", "8_despedida_sim", None, "10_despedida_nao"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vpk", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--langs", default="0,1,2,3,4,5,6,7,8"); ap.add_argument("--fit", action="store_true")
    a = ap.parse_args()
    r = Renderer(a.vpk, a.fit); out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    bad = 0
    for lang in map(int, a.langs.split(",")):
        for stage, name in enumerate(STAGE_NAMES):
            if name is None: continue
            img, rep = r.scene(lang, stage)
            img.save(out / f"{LANGS[lang]}_{name}.png")
            if stage == 0:
                img2, rep2 = r.scene(lang, 0, near=True)
                img2.save(out / f"{LANGS[lang]}_0b_perto_da_arvore.png"); rep += rep2
            for what, bb, lim in rep:
                if bb and (bb[0] < lim[0] or bb[1] < lim[1] or bb[2] > lim[2] or bb[3] > lim[3]):
                    bad += 1
                    print(f"OUT {LANGS[lang]:14} stage={stage:2} {what}: text=({bb[0]:.0f},{bb[1]:.0f})-({bb[2]:.0f},{bb[3]:.0f}) box=({lim[0]},{lim[1]})-({lim[2]},{lim[3]})")
    missing = sorted({c for lang in r.strings + [r.near] for s in lang for c in s if ord(c) > 32 and ord(c) not in r.glyphs})
    print("missing glyphs:", "".join(missing) or "none")
    print("out_of_box:", bad)

if __name__ == "__main__":
    main()
