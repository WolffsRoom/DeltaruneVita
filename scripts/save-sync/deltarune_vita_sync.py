#!/usr/bin/env python3
"""DELTARUNE Vita save sync (PC side, v0.84).

Keeps the PC (Steam) saves and the PS Vita port's saves equal over the local
network, without FTP. The Vita runs the server inside the game while the
option "Sync saves with PC" is on (Settings > System); this app finds it by
UDP broadcast and talks to it over TCP (docs/save-sync-protocol.md).

Rules (same spirit as the community DeltaruneSync tool):
  * only dr.ini, vacationmemories.png and filech<chapter>_<slot> are synced;
  * changed on one side since the last sync -> copied to the other side;
  * changed on both -> the side that changed most recently on this PC's clock
    wins (the PC watches both), the other copy goes to the backup folder;
    with --conflict ask the file is skipped and reported instead;
  * deletions are never synced; every overwrite is backed up first;
  * copies INTO the PC wait while DELTARUNE is running (Steam would keep its
    own copy in memory and overwrite it on the next save).

usage:
  deltarune_vita_sync.py                 tray app (falls back to console)
  deltarune_vita_sync.py --once          one sync pass and exit
  deltarune_vita_sync.py --console       keep syncing in this console
  options: --pc-dir DIR  --vita IP  --state-dir DIR  --conflict newest|ask
"""
import argparse, hashlib, json, os, re, socket, subprocess, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pathlib import Path

DISCOVERY_PORT = 26540
TCP_PORT = 26541
PROTOCOL = 1
SAVE_RE = re.compile(r"^(dr\.ini|vacationmemories\.png|filech\d+_\d+)$")
DEFAULT_PC = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "DELTARUNE"
DEFAULT_STATE = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "DeltaruneVitaSync"

def is_save(name):
    return bool(SAVE_RE.match(name))

def canonical(name, data):
    """dr.ini numbers differ by platform ("1" vs "1.000000") and key order may
    change; compare a normalised form so equal content is not ping-ponged."""
    if name != "dr.ini":
        return data
    out = {}
    section = ""
    for raw in data.decode("utf-8", "replace").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if "=" in line:
            k, v = line.split("=", 1)
            v = v.strip().strip('"')
            try:
                f = float(v)
                v = repr(int(f)) if f == int(f) else repr(f)
            except ValueError:
                pass
            out[(section, k.strip())] = v
    return json.dumps(sorted(out.items())).encode()

def digest(name, data):
    return hashlib.sha1(canonical(name, data)).hexdigest()

def game_running():
    if os.name != "nt":
        return False
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq DELTARUNE.exe", "/NH"],
                             capture_output=True, text=True, timeout=5).stdout
        return "DELTARUNE.exe" in out
    except Exception:
        return False

def log(state_dir, *a):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + " ".join(str(x) for x in a)
    print(line, flush=True)
    try:
        with open(state_dir / "sync.log", "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except OSError:
        pass

# ---------------------------------------------------------------- protocol
class Vita:
    def __init__(self, ip):
        self.ip = ip
        self.sock = socket.create_connection((ip, TCP_PORT), timeout=10)
        self.buf = b""
        self._send(b"HELLO DRSYNC %d\n" % PROTOCOL)
        reply = self._line()
        if not reply.startswith("OK DRSYNC"):
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
        return line.decode("utf-8", "replace").strip()

    def _exact(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise IOError("connection closed")
            self.buf += chunk
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def list(self):
        self._send(b"LIST\n")
        files = {}
        while True:
            line = self._line()
            if line == "END":
                return files
            parts = line.split()
            if len(parts) == 4 and parts[0] == "FILE" and is_save(parts[1]):
                files[parts[1]] = (int(parts[2]), parts[3])

    def get(self, name):
        self._send(("GET %s\n" % name).encode())
        head = self._line()
        if not head.startswith("DATA "):
            raise IOError("GET %s: %s" % (name, head))
        return self._exact(int(head.split()[1]))

    def put(self, name, data):
        self._send(("PUT %s %d\n" % (name, len(data))).encode() + data)
        reply = self._line()
        if reply != "OK":
            raise IOError("PUT %s: %s" % (name, reply))

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
            if data.startswith(b"DRSYNC "):
                return addr[0]
    except socket.timeout:
        return None
    finally:
        s.close()

# ---------------------------------------------------------------- sync
class Syncer:
    def __init__(self, pc_dir, state_dir, conflict="newest"):
        self.pc_dir = Path(pc_dir)
        self.state_dir = Path(state_dir)
        self.state_dir.mkdir(parents=True, exist_ok=True)
        self.conflict = conflict
        self.state_file = self.state_dir / "state.json"
        try:
            self.state = json.loads(self.state_file.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            self.state = {"files": {}}
        self.vita_seen = self.state.setdefault("vita_seen", {})   # name -> (crc, first time seen changed)
        self.events = []

    def save_state(self):
        tmp = self.state_file.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.state, indent=1, sort_keys=True), encoding="utf-8")
        os.replace(tmp, self.state_file)

    def backup(self, side, name, data):
        folder = self.state_dir / "backups" / time.strftime("%Y%m%d-%H%M%S")
        folder.mkdir(parents=True, exist_ok=True)
        (folder / ("%s-%s" % (side, name))).write_bytes(data)

    def pc_files(self):
        out = {}
        if self.pc_dir.is_dir():
            for p in self.pc_dir.iterdir():
                if p.is_file() and is_save(p.name):
                    out[p.name] = (p.read_bytes(), p.stat().st_mtime)
        return out

    def run(self, vita):
        now = time.time()
        remote = vita.list()
        local = self.pc_files()
        known = self.state["files"]
        busy = game_running()
        changes = 0
        for name in sorted(set(remote) | set(local)):
            last = known.get(name)                    # digest at the last sync
            pc = local.get(name)
            pc_digest = digest(name, pc[0]) if pc else None
            vita_crc = remote[name][1] if name in remote else None
            seen = self.vita_seen.get(name)
            if vita_crc is not None and (seen is None or seen[0] != vita_crc):
                self.vita_seen[name] = [vita_crc, now]   # when this PC first saw the Vita change
            vita_changed = vita_crc is not None and (last is None or last.get("vita_crc") != vita_crc)
            pc_changed = pc is not None and (last is None or last.get("pc") != pc_digest)
            if not vita_changed and not pc_changed:
                continue
            vdata = vita.get(name) if vita_changed else None
            if vdata is not None and pc is not None and digest(name, vdata) == pc_digest:
                known[name] = {"pc": pc_digest, "vita_crc": vita_crc}   # equal content
                continue
            winner = None
            if vita_changed and pc_changed and last is not None:
                if self.conflict == "ask":
                    log(self.state_dir, "CONFLICT", name, "changed on both; skipped (run with --prefer)")
                    continue
                vita_time = self.vita_seen[name][1]
                winner = "pc" if pc[1] >= vita_time else "vita"
            elif vita_changed:
                winner = "vita"
            else:
                winner = "pc"
            if winner == "vita":
                if busy:
                    log(self.state_dir, "WAIT", name, "DELTARUNE is open on the PC")
                    self.events.append("wait")
                    continue
                if pc is not None:
                    self.backup("pc", name, pc[0])
                tmp = self.pc_dir / (name + ".sync_tmp")
                self.pc_dir.mkdir(parents=True, exist_ok=True)
                tmp.write_bytes(vdata)
                os.replace(tmp, self.pc_dir / name)
                known[name] = {"pc": digest(name, vdata), "vita_crc": vita_crc}
                log(self.state_dir, "VITA -> PC", name, len(vdata), "bytes")
                self.events.append("to_pc")
            else:
                if vita_crc is not None:
                    self.backup("vita", name, vdata if vdata is not None else vita.get(name))
                vita.put(name, pc[0])
                new_crc = "%08x" % (__import__("zlib").crc32(pc[0]) & 0xFFFFFFFF)
                known[name] = {"pc": pc_digest, "vita_crc": new_crc}
                log(self.state_dir, "PC -> VITA", name, len(pc[0]), "bytes")
                self.events.append("to_vita")
            changes += 1
        self.save_state()
        return changes

# ---------------------------------------------------------------- runners
def connect(args, state_dir):
    ip = args.vita or discover()
    if ip is None:
        return None
    try:
        return Vita(ip)
    except OSError as e:
        log(state_dir, "connect failed:", e)
        return None

def loop(args, stop, status=None, wake=None):
    """status: dict shown by the window/tray (text without digits: the
    DELTARUNE pixel font atlas has none). wake: Event for "sync now"."""
    status = status if status is not None else {}
    syncer = Syncer(args.pc_dir, args.state_dir, args.conflict)
    vita = None
    while not stop.is_set():
        if vita is None:
            status.update(connected=False, line="Procurando o PS Vita na rede...",
                          sub="Ligue Sincronizar saves no jogo")
            vita = connect(args, syncer.state_dir)
            if vita is None:
                (wake or stop).wait(3)
                if wake is not None: wake.clear()
                continue
            log(syncer.state_dir, "connected to Vita", vita.ip)
            status.update(connected=True, line="Conectado ao PS Vita", sub="")
        try:
            before = len(syncer.events)
            changes = syncer.run(vita)
            new = syncer.events[before:]
            if any(e == "wait" for e in new):
                status["sub"] = "Aguardando o DELTARUNE fechar no PC"
            elif any(e == "to_pc" for e in new):
                status["sub"] = "Save recebido do PS Vita"
            elif any(e == "to_vita" for e in new):
                status["sub"] = "Save enviado para o PS Vita"
            elif changes == 0 and not status.get("sub"):
                status["sub"] = "Saves iguais nos dois lados"
        except OSError as e:
            log(syncer.state_dir, "Vita disconnected:", e)
            vita = None
            continue
        (wake or stop).wait(3)
        if wake is not None: wake.clear()
    if vita is not None:
        vita.close()

def tray(args):
    try:
        import drsync_ui
    except Exception as e:   # Pillow/tkinter missing
        print("window unavailable (%s): running in the console" % e)
        return console(args)
    stop, wake = threading.Event(), threading.Event()
    status = {}
    threading.Thread(target=loop, args=(args, stop, status, wake), daemon=True).start()
    drsync_ui.run(status, wake.set, stop, lambda: os.startfile(str(args.state_dir)))
def console(args):
    stop = threading.Event()
    try:
        loop(args, stop)
    except KeyboardInterrupt:
        stop.set()

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pc-dir", type=Path, default=DEFAULT_PC)
    ap.add_argument("--state-dir", type=Path, default=DEFAULT_STATE)
    ap.add_argument("--vita", help="Vita IP (skips discovery)")
    ap.add_argument("--conflict", choices=["newest", "ask"], default="newest")
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--console", action="store_true")
    args = ap.parse_args()
    args.state_dir.mkdir(parents=True, exist_ok=True)
    if args.once:
        syncer = Syncer(args.pc_dir, args.state_dir, args.conflict)
        vita = connect(args, syncer.state_dir)
        if vita is None:
            print("Vita not found (is the game open with 'Sync saves with PC' on?)"); return 1
        n = syncer.run(vita); vita.close()
        print("changes:", n); return 0
    return console(args) if args.console else tray(args)

if __name__ == "__main__":
    sys.exit(main() or 0)
