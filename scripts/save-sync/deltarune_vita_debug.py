#!/usr/bin/env python3
"""DELTARUNE Debug: developer companion for Deltarune Vita (v0.84-3).

With "Send reports locally" on in the game's DEBUG tab and the PC on the same
network (Wi-Fi or Ethernet), this app:
  * downloads every NEW devlog (ux0:data/deltarune/deltarunevita/devlogs/) and
    every NEW screenshot (ux0:picture/SCREENSHOT/Deltarune/) made while it runs;
  * shows the active probe log live (native-gxm-probe.log when Native GXM is in
    use, butterscotch-probe.log otherwise) and keeps a copy on the PC.

Files go to Documents\\DeltaruneVita Debug\\ (devlogs\\, screenshots\\, live\\).
Protocol: docs/debug-sync-protocol.md. Nothing is ever written to the Vita.
"""
import argparse, collections, os, socket, sys, threading, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

DISCOVERY_PORT = 26550
TCP_PORT = 26551
PROTOCOL = 1
DEFAULT_OUT = Path(os.environ.get("USERPROFILE", str(Path.home()))) / "Documents" / "DeltaruneVita Debug"
DEFAULT_STATE = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "DeltaruneVitaDebug"
LIST_EVERY = 5.0      # seconds between devlog/screenshot listings
TAIL_EVERY = 1.0      # seconds between live log reads
LIVE_CONTEXT = 16 * 1024   # bytes of the log shown when the live view starts

def log(state_dir, *a):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + " ".join(str(x) for x in a)
    print(line, flush=True)
    try:
        with open(state_dir / "debug.log", "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except OSError:
        pass

# ---------------------------------------------------------------- protocol
class Vita:
    def __init__(self, ip):
        self.ip = ip
        self.sock = socket.create_connection((ip, TCP_PORT), timeout=15)
        self.buf = b""
        self._send(b"HELLO DRDEBUG %d\n" % PROTOCOL)
        reply = self._line()
        if not reply.startswith("OK DRDEBUG"):
            raise IOError("unexpected hello: " + reply)

    def _send(self, b):
        self.sock.sendall(b)

    def _line(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise IOError("connection closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("utf-8", "replace").rstrip("\r")

    def _exact(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise IOError("connection closed")
            self.buf += chunk
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def list(self):
        """{(area, relpath): (size, mtime)} for devlogs and screenshots."""
        self._send(b"LIST\n")
        files = {}
        while True:
            line = self._line()
            if line == "END":
                return files
            parts = line.split(" ", 4)
            if len(parts) == 5 and parts[0] == "FILE" and parts[1] in ("devlog", "shot"):
                files[(parts[1], parts[4])] = (int(parts[2]), int(parts[3]))

    def logs(self):
        self._send(b"LOGS\n")
        out = {}
        while True:
            line = self._line()
            if line == "END":
                return out
            parts = line.split()
            if len(parts) == 4 and parts[0] == "LOG":
                out[parts[1]] = (int(parts[2]), int(parts[3]))

    def get(self, area, rel):
        self._send(("GET %s %s\n" % (area, rel)).encode("utf-8"))
        head = self._line()
        if not head.startswith("DATA "):
            raise FileNotFoundError("GET %s %s: %s" % (area, rel, head))
        return self._exact(int(head.split()[1]))

    def tail(self, name, offset):
        """(bytes, total size, reset) from offset of a probe log."""
        self._send(("TAIL %s %d\n" % (name, offset)).encode())
        head = self._line().split()
        if len(head) < 3 or head[0] != "DATA":
            raise IOError("TAIL: " + " ".join(head))
        data = self._exact(int(head[1]))
        return data, int(head[2]), "RESET" in head

    def close(self):
        try:
            self._send(b"BYE\n")
        except OSError:
            pass
        self.sock.close()

def discover(timeout=6.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", DISCOVERY_PORT))
    s.settimeout(timeout)
    try:
        while True:
            data, addr = s.recvfrom(256)
            if data.startswith(b"DRDEBUG "):
                return addr[0]
    except socket.timeout:
        return None
    finally:
        s.close()

# ---------------------------------------------------------------- collector
class Collector:
    """Downloads files that appear after the app started, once their size is
    stable across two listings (the game may still be writing them)."""
    def __init__(self, out_dir, state_dir):
        self.out, self.state = Path(out_dir), Path(state_dir)
        self.baseline = None          # listing at the first connection: never downloaded
        self.done = {}                # key -> (size, mtime) already saved
        self.pending = {}             # key -> (size, mtime) seen once, waiting to settle
        self.events = []              # "devlog" / "shot" per saved file

    def target(self, area, rel):
        base = self.out / ("devlogs" if area == "devlog" else "screenshots")
        return base.joinpath(*rel.split("/"))

    def run(self, vita):
        listing = vita.list()
        if self.baseline is None:
            self.baseline = dict(listing)
            log(self.state, "baseline: %d existing files ignored" % len(listing))
            return 0
        saved = 0
        for key, meta in sorted(listing.items()):
            if self.baseline.get(key) == meta or self.done.get(key) == meta:
                continue
            if self.pending.get(key) != meta:
                self.pending[key] = meta          # new or still growing: wait one round
                continue
            area, rel = key
            try:
                data = vita.get(area, rel)
            except FileNotFoundError as e:
                log(self.state, "skip:", e); self.pending.pop(key, None); continue
            path = self.target(area, rel)
            path.parent.mkdir(parents=True, exist_ok=True)
            tmp = path.with_name(path.name + ".part")
            tmp.write_bytes(data)
            os.replace(tmp, path)
            self.done[key] = meta
            self.pending.pop(key, None)
            self.events.append(area)
            saved += 1
            log(self.state, "saved", area, rel, len(data), "bytes")
        return saved

class LiveLog:
    """Follows the most recently written probe log."""
    def __init__(self, out_dir, sink):
        self.out = Path(out_dir) / "live"
        self.sink = sink              # callable(name, text, reset)
        self.name, self.offset = None, 0

    def pick(self, vita):
        logs = vita.logs()
        if not logs:
            return
        name = max(logs, key=lambda n: logs[n][1])
        if name != self.name:
            self.name = name
            self.offset = max(0, logs[name][0] - LIVE_CONTEXT)
            self.sink(name, "", True)

    def poll(self, vita):
        if self.name is None:
            return
        data, total, reset = vita.tail(self.name, self.offset)
        if reset:
            self.offset = 0
            self.sink(self.name, "", True)
        if data:
            self.offset += len(data)
            text = data.decode("utf-8", "replace")
            self.sink(self.name, text, False)
            try:
                self.out.mkdir(parents=True, exist_ok=True)
                with open(self.out / self.name, "a", encoding="utf-8") as f:
                    f.write(text)
            except OSError:
                pass

def loop(args, stop, status=None, sink=None):
    status = status if status is not None else {}
    sink = sink or (lambda name, text, reset: sys.stdout.write(text))
    col = Collector(args.out_dir, args.state_dir)
    live = LiveLog(args.out_dir, sink)
    vita, next_list, next_pick = None, 0.0, 0.0
    while not stop.is_set():
        if vita is None:
            status.update(connected=False, line="searching", sub="dbg_sub")
            ip = args.vita or discover()
            if ip is None:
                stop.wait(2); continue
            try:
                vita = Vita(ip)
            except OSError as e:
                log(args.state_dir, "connect failed:", e); stop.wait(3); continue
            log(args.state_dir, "connected to Vita", ip)
            status.update(connected=True, line="connected", sub="dbg_watch", ip=ip)
            live.name = None
            next_list = next_pick = 0.0
        try:
            now = time.time()
            if now >= next_pick:
                live.pick(vita); next_pick = now + 5.0
            live.poll(vita)
            if now >= next_list:
                before = len(col.events)
                if col.run(vita):
                    new = col.events[before:]
                    status["copied"] = status.get("copied", 0) + len(new)
                    status["sub"] = "dbg_shot" if new[-1] == "shot" else "dbg_log"
                next_list = now + LIST_EVERY
        except OSError as e:
            log(args.state_dir, "Vita disconnected:", e)
            try: vita.close()
            except OSError: pass
            vita = None
            continue
        stop.wait(TAIL_EVERY)
    if vita is not None:
        vita.close()

# ---------------------------------------------------------------- UI
def live_window_setup(holder):
    """Returns setup(win) for drsync_ui.run: a dark log window fed by holder."""
    def setup(win):
        import tkinter as tk
        top = tk.Toplevel(win.root)
        top.title("DELTARUNE Debug - log")
        top.configure(bg="black")
        top.geometry("980x560")
        bar = tk.Frame(top, bg="black"); bar.pack(fill="x")
        title = tk.Label(bar, text="...", fg="#ffe13f", bg="black", font=("Consolas", 11, "bold"))
        title.pack(side="left", padx=8, pady=4)
        follow = tk.BooleanVar(value=True)
        tk.Checkbutton(bar, text="auto-scroll", variable=follow, fg="white", bg="black",
                       selectcolor="black", activebackground="black").pack(side="right", padx=6)
        text = tk.Text(top, bg="#0b0b0b", fg="#e6e6e6", insertbackground="white",
                       font=("Consolas", 10), wrap="none", borderwidth=0)
        ys = tk.Scrollbar(top, command=text.yview); text.configure(yscrollcommand=ys.set)
        ys.pack(side="right", fill="y"); text.pack(fill="both", expand=True)
        text.tag_configure("err", foreground="#ff6b6b")
        text.tag_configure("warn", foreground="#ffd166")
        def pump():
            q = holder["queue"]
            changed = False
            while q:
                name, chunk, reset = q.popleft()
                title.configure(text=name)
                if reset:
                    text.delete("1.0", "end")
                for line in chunk.splitlines(True):
                    up = line.upper()
                    tag = "err" if ("ERROR" in up or "CRASH" in up or "FAIL" in up) else \
                          ("warn" if ("WARN" in up or "STALL" in up) else None)
                    text.insert("end", line, tag)
                changed = True
            if changed:
                extra = int(text.index("end-1c").split(".")[0]) - 6000
                if extra > 0:
                    text.delete("1.0", "%d.0" % extra)
                if follow.get():
                    text.see("end")
            top.after(250, pump)
        top.protocol("WM_DELETE_WINDOW", top.withdraw)
        holder["show"] = lambda: (top.deiconify(), top.lift())
        top.withdraw()
        pump()
    return setup

def tray(args):
    try:
        import drsync_ui
    except Exception as e:
        print("window unavailable (%s): running in the console" % e)
        return console(args)
    drsync_ui.configure(name="DELTARUNE Debug", tray_id="DeltaruneDebug",
                        title="title_debug.png", heart=(255, 255, 0))
    stop = threading.Event()
    status = {}
    holder = {"queue": collections.deque(), "show": lambda: None}
    sink = lambda name, text, reset: holder["queue"].append((name, text, reset))
    threading.Thread(target=loop, args=(args, stop, status, sink), daemon=True).start()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    open_folder = lambda: os.startfile(str(args.out_dir))
    show_live = lambda: holder["show"]()
    drsync_ui.run(status, show_live, stop, open_folder, args.state_dir / "settings.json", args.lang,
                  actions=[("live", show_live), ("folder", open_folder), ("lang", None), ("hide", None)],
                  tray_extra=("live", show_live), setup=live_window_setup(holder))

def console(args):
    stop = threading.Event()
    try:
        loop(args, stop)
    except KeyboardInterrupt:
        stop.set()

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--state-dir", type=Path, default=DEFAULT_STATE)
    ap.add_argument("--vita", help="Vita IP (skips discovery)")
    ap.add_argument("--lang", help="UI language")
    ap.add_argument("--console", action="store_true")
    args = ap.parse_args()
    args.state_dir.mkdir(parents=True, exist_ok=True)
    return console(args) if args.console else tray(args)

if __name__ == "__main__":
    sys.exit(main() or 0)
