"""DELTARUNE Vita Sync: window and tray icon in the game's own style.

The window is a DELTARUNE dialogue box (the game's border art and font) with
"DELTARUNE SYNC" in the middle, the connection status and a small menu with
the SOUL heart as the cursor. The tray icon is the same heart; it beats
gently while the Vita is connected. Closing the window keeps the app in the
tray. Assets live in assets/ next to this file.
"""
import json, math, os, sys, threading
import drsync_i18n as i18n
from pathlib import Path
import tkinter as tk
from PIL import Image, ImageTk

HERE = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
ASSETS = HERE / "assets"
YELLOW = (255, 225, 63)
WHITE = (255, 255, 255)
GREY = (150, 150, 160)

class PixelFont:
    """Glyph atlas generated from the DELTARUNE font (same as the Vita prologue).
    Fallback glyphs (kind 1/2) sit on the Latin baseline, as in v0.82."""
    def __init__(self):
        self.atlas = Image.open(ASSETS / "font_glyphs.png").convert("RGBA")
        self.glyphs = {int(k): v for k, v in json.load(open(ASSETS / "font_glyphs.json")).items()}
        # The atlas only has the Cyrillic/Japanese letters the prologue uses, so
        # those scripts always come from the system font instead of mixing styles.
        self.glyphs = {k: v for k, v in self.glyphs.items() if k < 0x400 and v[7] == 0}

        # Japanese: MS Gothic (pixel-like CJK). Cyrillic and the rest: a bold
        # proportional Windows font, since MS Gothic draws Cyrillic full-width.
        self.fallback = self._font(("msgothic.ttc", "YuGothM.ttc", "meiryo.ttc", "arial.ttf"), 24)
        self.fallback_west = self._font(("tahomabd.ttf", "arialbd.ttf", "arial.ttf"), 25) or self.fallback

    @staticmethod
    def _font(names, size):
        from PIL import ImageFont
        for name in names:
            try:
                return ImageFont.truetype(os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts", name), size)
            except Exception:
                continue
        return None

    def _fb(self, c):
        return self.fallback if ord(c) >= 0x3000 else self.fallback_west

    def _adv(self, c):
        g = self.glyphs.get(ord(c))
        if g is not None or self.fallback is None or c == " ":
            return (g or self.glyphs[ord(" ")])[6]
        return int(round(self._fb(c).getlength(c))) + (3 if ord(c) >= 0x3000 else 2)

    def width(self, text):
        return sum(self._adv(c) for c in text)

    def render(self, text, color=WHITE, scale=1):
        w = max(1, self.width(text)); h = 34
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        x = 0
        for c in text:
            g = self.glyphs.get(ord(c))
            if g is None and self.fallback is not None and c != " ":
                from PIL import ImageDraw
                adv = self._adv(c)
                cell = Image.new("L", (adv + 2, h), 0)
                # Cyrillic gets a 1 px stroke to match the bold pixel font; dense
                # Japanese glyphs would fill in, so they keep the plain weight.
                cjk = ord(c) >= 0x3000
                ImageDraw.Draw(cell).text((1, 1 if cjk else 1), c, font=self._fb(c), fill=255)
                tint = Image.new("RGBA", cell.size, color + (255,))
                tint.putalpha(cell.point(lambda a: 255 if a > 110 else 0))  # hard edges like the pixel font
                img.alpha_composite(tint, (x, 0))
                x += adv
                continue
            g = g or self.glyphs[ord("?")]
            gx, gy, gw, gh, bx, by, adv, kind = g
            if gw and gh:
                crop = self.atlas.crop((gx, gy, gx + gw, gy + gh))
                tint = Image.new("RGBA", crop.size, color + (255,))
                tint.putalpha(crop.getchannel("A"))
                shift = 17 if kind == 2 else (13 if kind == 1 else 0)
                img.alpha_composite(tint, (x + bx, max(0, by + shift)))
            x += adv
        if scale != 1:
            img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        return img

def heart_frames(size=64):
    """Tray icons: the SOUL heart at rest and a gentle beat (8 frames)."""
    heart = Image.open(ASSETS / "heart.png").convert("RGBA")
    frames = []
    for k in range(8):
        beat = 1.0 + 0.10 * max(0.0, math.sin(k / 8.0 * 2 * math.pi))
        s = int(size * 0.78 * beat)
        h = heart.resize((s, s), Image.NEAREST)
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        img.alpha_composite(h, ((size - s) // 2, (size - s) // 2))
        frames.append(img)
    idle = frames[0].copy()
    idle.putalpha(idle.getchannel("A").point(lambda a: a * 55 // 100))
    return frames, idle

def _dpi_aware():
    """Crisp pixel art: tell Windows the window draws its own pixels, so it is
    not bitmap-stretched (blurry) at 125-150 % display scaling."""
    try:
        import ctypes
        if ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)):
            return
    except Exception:
        pass
    try:
        import ctypes; ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        try:
            import ctypes; ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass

class Sounds:
    """Game sound effects (assets/sfx), played asynchronously with winsound.
    winsound plays one sound at a time, like the game's text voice cutting
    itself off on every letter."""
    def __init__(self):
        self.ok = False
        try:
            import winsound
            self.winsound = winsound
            self.ok = True
        except ImportError:
            pass

    def play(self, name):
        if not self.ok:
            return
        path = ASSETS / "sfx" / (name + ".wav")
        if path.exists():
            w = self.winsound
            w.PlaySound(str(path), w.SND_FILENAME | w.SND_ASYNC | w.SND_NODEFAULT)

class Typewriter:
    """Reveals one character per frame (30 per second, as the game's text) and
    plays the text voice on each letter."""
    def __init__(self, sounds):
        self.sounds = sounds
        self.target = ""
        self.shown = 0
        self.delay = 0

    def set(self, text, delay=0):
        if text != self.target:
            self.target, self.shown, self.delay = text, 0, delay

    def step(self):
        if self.delay > 0:
            self.delay -= 1
            return False
        if self.shown < len(self.target):
            self.shown += 1
            if self.target[self.shown - 1] not in " .*":
                self.sounds.play("text")
            return False
        return True

    def text(self):
        return self.target[:self.shown]

class SyncWindow:
    W, H = 900, 340
    TICK_MS = 33

    def __init__(self, status, sync_now, quit_app, open_backups, settings_path=None, lang=None):
        self.status, self.sync_now, self.quit_app, self.open_backups = status, sync_now, quit_app, open_backups
        self.settings_path = settings_path
        saved = {}
        try:
            saved = json.load(open(settings_path, encoding="utf-8")) if settings_path else {}
        except Exception:
            pass
        self.lang = lang if lang in i18n.LANGS else saved.get("lang") if saved.get("lang") in i18n.LANGS \
            else i18n.system_language()
        _dpi_aware()
        self.font = PixelFont()
        self.sounds = Sounds()
        self.line_tw = Typewriter(self.sounds)
        self.sub_tw = Typewriter(self.sounds)
        self.was_connected = None
        self.root = tk.Tk()
        self.root.title("DELTARUNE Sync")
        self.root.configure(bg="black")
        self.root.resizable(False, False)
        x = (self.root.winfo_screenwidth() - self.W) // 2
        y = (self.root.winfo_screenheight() - self.H) // 2
        self.root.geometry("%dx%d+%d+%d" % (self.W, self.H, x, y))
        try:
            self._icon = ImageTk.PhotoImage(heart_frames(32)[0][0])
            self.root.iconphoto(True, self._icon)
        except Exception:
            pass
        self.canvas = tk.Canvas(self.root, width=self.W, height=self.H, bg="black",
                                highlightthickness=0)
        self.canvas.pack()
        box = Image.open(ASSETS / "dialogbox.png").convert("RGBA")
        # Integer-ish scale of the game's box: 598x172 -> 861x248, centred.
        self.box = ImageTk.PhotoImage(box.resize((int(box.width * 1.44), int(box.height * 1.44)), Image.NEAREST))
        self.box_top = (self.H - self.box.height()) // 2
        # Title rendered once at exact 2x (pixel-doubled, like the game's 640x480 art).
        self.title_img = ImageTk.PhotoImage(self.font.render("DELTARUNE SYNC", YELLOW, 2))
        self.heart_img = ImageTk.PhotoImage(Image.open(ASSETS / "heart.png").convert("RGBA").resize((18, 18), Image.NEAREST))
        self.option_keys = ["sync", "backups", "lang", "hide"]
        self.selected = 0
        self.frame = 0
        self._images = []
        self.root.protocol("WM_DELETE_WINDOW", self.hide)
        for key in ("<Up>", "<Left>"): self.root.bind(key, lambda e: self.move(-1))
        for key in ("<Down>", "<Right>"): self.root.bind(key, lambda e: self.move(1))
        for key in ("<Return>", "<z>", "<Z>", "<space>"): self.root.bind(key, lambda e: self.activate())
        self.canvas.bind("<Button-1>", self.click)
        self.canvas.bind("<Motion>", self.hover)
        self.option_boxes = []
        self.tick()

    def text(self, s, cx, y, color=WHITE, scale=1, anchor_center=True):
        if not s:
            return cx, y, 0, 0
        img = ImageTk.PhotoImage(self.font.render(s, color, scale))
        self._images.append(img)
        x = cx - img.width() // 2 if anchor_center else cx
        self.canvas.create_image(int(x), int(y), image=img, anchor="nw")
        return x, y, img.width(), img.height()

    def update_status(self):
        st = self.status
        connected = bool(st.get("connected"))
        if self.was_connected is not None and connected != self.was_connected:
            self.sounds.play("connect" if connected else "disconnect")
            delay = 22   # let the sound be heard before the voice starts
        else:
            delay = 0
        self.was_connected = connected
        self.line_tw.set("* " + i18n.tr(st.get("line", "searching"), self.lang), delay)
        if self.line_tw.step():
            sub = st.get("sub", "")
            self.sub_tw.set(i18n.tr(sub, self.lang) if sub else "")
            self.sub_tw.step()
        else:
            self.sub_tw.set("")

    def draw(self):
        c = self.canvas; c.delete("all"); self._images = []; self.option_boxes = []
        c.create_image(self.W // 2, self.H // 2, image=self.box)
        top = self.box_top
        bounce = int(round(abs(math.sin(self.frame * 0.04)) * 3))
        c.create_image(self.W // 2 - self.title_img.width() // 2, top + 30 - bounce,
                       image=self.title_img, anchor="nw")
        # Typed lines are left-aligned on the centred full line, so letters do
        # not slide sideways while they appear.
        full = self.line_tw.target
        lx = (self.W - self.font.width(full)) // 2
        self.text(self.line_tw.text(), lx, top + 104, WHITE, anchor_center=False)
        sfull = self.sub_tw.target
        sx = (self.W - self.font.width(sfull)) // 2
        self.text(self.sub_tw.text(), sx, top + 138, GREY, anchor_center=False)
        options = [i18n.NAMES[self.lang] if k == "lang" else i18n.tr(k, self.lang)
                   for k in self.option_keys]
        gap = 56
        total = sum(self.font.width(o) for o in options) + gap * (len(options) - 1)
        x = (self.W - total) // 2
        for i, o in enumerate(options):
            sel = i == self.selected
            bx, by, bw, bh = self.text(o, x, top + 188, YELLOW if sel else WHITE, anchor_center=False)
            self.option_boxes.append((bx - 30, by - 6, bx + bw + 6, by + bh))
            if sel:
                c.create_image(int(bx) - 26, int(by) + 8, image=self.heart_img, anchor="nw")
            x += bw + gap

    def tick(self):
        self.frame += 1
        self.update_status()
        self.draw()
        self.root.after(self.TICK_MS, self.tick)

    def move(self, d):
        self.selected = (self.selected + d) % len(self.option_keys)
        self.sounds.play("move")

    def next_language(self):
        self.lang = i18n.LANGS[(i18n.LANGS.index(self.lang) + 1) % len(i18n.LANGS)]
        self.line_tw.set(""); self.sub_tw.set("")       # retype in the new language
        if self.settings_path:
            try:
                with open(self.settings_path, "w", encoding="utf-8") as f:
                    json.dump({"lang": self.lang}, f)
            except OSError:
                pass

    def activate(self):
        self.sounds.play("select")
        {"sync": self.sync_now, "backups": self.open_backups, "lang": self.next_language,
         "hide": self.hide}[self.option_keys[self.selected]]()

    def click(self, e):
        for i, (x0, y0, x1, y1) in enumerate(self.option_boxes):
            if x0 <= e.x <= x1 and y0 <= e.y <= y1:
                self.selected = i; self.activate(); return

    def hover(self, e):
        for i, (x0, y0, x1, y1) in enumerate(self.option_boxes):
            if x0 <= e.x <= x1 and y0 <= e.y <= y1 and self.selected != i:
                self.selected = i
                self.sounds.play("move")

    def hide(self):
        self.root.withdraw()

    def show(self):
        self.root.after(0, lambda: (self.root.deiconify(), self.root.lift(), self.root.focus_force()))
def run(status, sync_now, quit_flag, open_backups, settings_path=None, lang=None):
    """Window on the main thread; tray icon (pystray) on its own thread."""
    win = SyncWindow(status, sync_now, lambda: None, open_backups, settings_path, lang)
    tray_icon = None
    try:
        import pystray
        frames, idle = heart_frames(64)
        def on_quit(icon, _):
            quit_flag.set(); icon.stop(); win.root.after(0, win.root.destroy)
        tray_icon = pystray.Icon("DeltaruneSync", idle, "DELTARUNE Sync",
                                 menu=pystray.Menu(
                                     pystray.MenuItem(lambda _: i18n.tr("open", win.lang), lambda i, _: win.show(), default=True),
                                     pystray.MenuItem(lambda _: i18n.tr("sync_now", win.lang), lambda i, _: sync_now()),
                                     pystray.MenuItem(lambda _: i18n.tr("quit", win.lang), on_quit)))
        def animate():
            k = 0
            while not quit_flag.is_set():
                if status.get("connected"):
                    tray_icon.icon = frames[k % len(frames)]; k += 1
                    tray_icon.title = "DELTARUNE Sync - PS Vita " + status.get("ip", "")
                else:
                    tray_icon.icon = idle
                    tray_icon.title = "DELTARUNE Sync - " + i18n.tr("searching", win.lang)
                quit_flag.wait(0.12)
        threading.Thread(target=tray_icon.run, daemon=True).start()
        threading.Thread(target=animate, daemon=True).start()
    except ImportError:
        pass
    def on_close_app():
        if tray_icon is None:   # no tray: closing the window quits
            quit_flag.set(); win.root.destroy()
        else:
            win.hide()
    win.root.protocol("WM_DELETE_WINDOW", on_close_app)
    win.root.mainloop()
    quit_flag.set()
