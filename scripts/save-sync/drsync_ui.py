"""DELTARUNE Vita Sync: window and tray icon in the game's own style.

The window is a DELTARUNE dialogue box (the game's border art and font) with
"DELTARUNE SYNC" in the middle, the connection status and a small menu with
the SOUL heart as the cursor. The tray icon is the same heart; it beats
gently while the Vita is connected. Closing the window keeps the app in the
tray. Assets live in assets/ next to this file.
"""
import json, math, os, sys, threading
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

    def width(self, text):
        return sum(self.glyphs.get(ord(c), self.glyphs[ord("?")])[6] for c in text)

    def render(self, text, color=WHITE, scale=1):
        w = max(1, self.width(text)); h = 34
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        x = 0
        for c in text:
            g = self.glyphs.get(ord(c), self.glyphs[ord("?")])
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

class SyncWindow:
    W, H = 900, 300

    def __init__(self, status, sync_now, quit_app, open_backups):
        self.status, self.sync_now, self.quit_app, self.open_backups = status, sync_now, quit_app, open_backups
        self.font = PixelFont()
        try:  # crisp pixel art: no Windows bitmap scaling of the window
            import ctypes; ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:
            pass
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
        self.box = ImageTk.PhotoImage(box.resize((int(box.width * 1.44), int(box.height * 1.44)), Image.NEAREST))
        self.options = ["Sincronizar", "Backups", "Ocultar"]
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
        img = ImageTk.PhotoImage(self.font.render(s, color, scale))
        self._images.append(img)
        x = cx - img.width() // 2 if anchor_center else cx
        self.canvas.create_image(x, y, image=img, anchor="nw")
        return x, y, img.width(), img.height()

    def draw(self):
        c = self.canvas; c.delete("all"); self._images = []; self.option_boxes = []
        c.create_image(self.W // 2, self.H // 2, image=self.box)
        bounce = abs(math.sin(self.frame * 0.06)) * 3
        self.text("DELTARUNE SYNC", self.W // 2, 44 - bounce, YELLOW, scale=2)
        st = self.status
        line = st.get("line", "Procurando o PS Vita na rede...")
        self.text("* " + line, self.W // 2, 122, WHITE)
        sub = st.get("sub", "")
        if sub:
            self.text(sub, self.W // 2, 154, GREY)
        # menu row, heart cursor on the selected option
        total = sum(self.font.width(o) for o in self.options) + 80 * (len(self.options) - 1)
        x = (self.W - total) // 2
        for i, o in enumerate(self.options):
            sel = i == self.selected
            bx, by, bw, bh = self.text(o, x, 204, YELLOW if sel else WHITE, anchor_center=False)
            self.option_boxes.append((bx - 30, by - 6, bx + bw + 6, by + bh))
            if sel:
                heart = ImageTk.PhotoImage(Image.open(ASSETS / "heart.png").convert("RGBA").resize((18, 18), Image.NEAREST))
                self._images.append(heart)
                c.create_image(bx - 26, by + 8, image=heart, anchor="nw")
            x += bw + 80

    def tick(self):
        self.frame += 1
        self.draw()
        self.root.after(60, self.tick)

    def move(self, d):
        self.selected = (self.selected + d) % len(self.options)

    def activate(self):
        [self.sync_now, self.open_backups, self.hide][self.selected]()

    def click(self, e):
        for i, (x0, y0, x1, y1) in enumerate(self.option_boxes):
            if x0 <= e.x <= x1 and y0 <= e.y <= y1:
                self.selected = i; self.activate(); return

    def hover(self, e):
        for i, (x0, y0, x1, y1) in enumerate(self.option_boxes):
            if x0 <= e.x <= x1 and y0 <= e.y <= y1:
                self.selected = i

    def hide(self):
        self.root.withdraw()

    def show(self):
        self.root.after(0, lambda: (self.root.deiconify(), self.root.lift(), self.root.focus_force()))

def run(status, sync_now, quit_flag, open_backups):
    """Window on the main thread; tray icon (pystray) on its own thread."""
    win = SyncWindow(status, sync_now, lambda: None, open_backups)
    tray_icon = None
    try:
        import pystray
        frames, idle = heart_frames(64)
        def on_quit(icon, _):
            quit_flag.set(); icon.stop(); win.root.after(0, win.root.destroy)
        tray_icon = pystray.Icon("DeltaruneSync", idle, "DELTARUNE Sync",
                                 menu=pystray.Menu(
                                     pystray.MenuItem("Abrir", lambda i, _: win.show(), default=True),
                                     pystray.MenuItem("Sincronizar agora", lambda i, _: sync_now()),
                                     pystray.MenuItem("Sair", on_quit)))
        def animate():
            k = 0
            while not quit_flag.is_set():
                if status.get("connected"):
                    tray_icon.icon = frames[k % len(frames)]; k += 1
                    tray_icon.title = "DELTARUNE Sync - " + status.get("line", "")
                else:
                    tray_icon.icon = idle
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
