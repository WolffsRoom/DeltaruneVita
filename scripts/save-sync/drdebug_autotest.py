"""DELTARUNE Debug auto-test (v0.86).

Drives the game on the Vita through the debug server: for each room of a
plan it jumps there (same safe path as the DEV Room Navigator), waits for the
room to settle, records a devlog capture (DEV_SAMPLE + GML profiler) for N
seconds, follows the live probe log, then downloads the devlogs and writes a
report.

Plans: plain text, one room per line, optional seconds:  room_dw_cliff_kawkawdash 25
Lines starting with # are comments. Presets: autotest/ch1.txt ... ch5.txt.
The game must be open (in gameplay, any room of the plan's chapter) with
"Send reports locally" on. Needs no input on the Vita; battles and scripted
events are not triggered (rooms only)."""
import re, time
from pathlib import Path

HERE = Path(__file__).resolve().parent

def load_plan(spec, default_seconds):
    path = Path(spec)
    if not path.exists():
        path = HERE / "autotest" / (spec if spec.endswith(".txt") else spec + ".txt")
    rooms = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        secs = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else default_seconds
        rooms.append((parts[0], secs))
    return path, rooms

def parse_status(line):
    out = {}
    for m in re.finditer(r"(\w+)=(\S+)", line):
        out[m.group(1)] = m.group(2)
    return out

def summarize_devlog(text):
    fps, total, slow = [], [], 0
    for m in re.finditer(r"DEV_SAMPLE frame=\d+ room=\S+ fps=([\d.]+) total_us=(\d+)", text):
        fps.append(float(m.group(1)))
        total.append(int(m.group(2)))
    for m in re.finditer(r"PERF_SLOW frame=\d+ total_us=(\d+)", text):
        slow += 1
    gml = re.findall(r"^\s*([\d.]+)ms \d+ ops \(\d+ ns/op\) (\S+)", text, re.M)
    top = {}
    for ms, name in gml:
        top.setdefault(name, []).append(float(ms))
    top_avg = sorted(((sum(v) / len(v), k) for k, v in top.items()), reverse=True)[:5]
    return {
        "samples": len(fps),
        "fps_avg": sum(fps) / len(fps) if fps else 0.0,
        "fps_min": min(fps) if fps else 0.0,
        "frame_ms_avg": sum(total) / len(total) / 1000.0 if total else 0.0,
        "frame_ms_max": max(total) / 1000.0 if total else 0.0,
        "slow": slow,
        "gml_top": top_avg,
    }

def run(vita, plan_spec, out_root, log, default_seconds=20, settle=4):
    """vita: a connected deltarune_vita_debug.Vita. Returns the report path."""
    plan_path, rooms = load_plan(plan_spec, default_seconds)
    run_dir = Path(out_root) / "autotest" / time.strftime("%Y%m%d-%H%M%S")
    run_dir.mkdir(parents=True, exist_ok=True)
    known = set(vita.rooms())
    status = parse_status(vita.status())
    log("auto-test: plan=%s rooms=%d chapter=%s build=%s" % (plan_path.name, len(rooms),
        status.get("chapter", "?"), status.get("build", "?")))
    before = set(vita.list())
    live_name, live_offset = None, 0
    logs = vita.logs()
    if logs:
        live_name = max(logs, key=lambda n: logs[n][1])
        live_offset = logs[live_name][0]
    live_file = open(run_dir / "probe-during-test.log", "a", encoding="utf-8")

    def pump_live():
        nonlocal live_offset
        if live_name is None:
            return
        data, total, reset = vita.tail(live_name, live_offset)
        if reset:
            live_offset = 0
        live_offset += len(data)
        if data:
            live_file.write(data.decode("utf-8", "replace"))
            live_file.flush()

    def wait(seconds, until=None):
        end = time.time() + seconds
        st = {}
        while time.time() < end:
            pump_live()
            st = parse_status(vita.status())
            if until is not None and until(st):
                return st
            time.sleep(1.0)
        return st

    results = []
    for room, secs in rooms:
        entry = {"room": room, "seconds": secs, "result": "", "devlogs": []}
        if known and room not in known:
            entry["result"] = "not in this chapter"
            results.append(entry); log("  %-40s skipped (not in this chapter)" % room)
            continue
        vita.command("MARK autotest_" + room)
        prev_last = parse_status(vita.status()).get("last", "")
        vita.command("ROOM " + room)

        def arrived(s):
            last = s.get("last", "")
            if s.get("pending") != "0" or last == prev_last:
                return False
            if last.startswith("room_jump:"):
                return s.get("room") == last.split(":", 1)[1]     # transition finished
            return last.startswith(("room_already:", "room_unavailable:", "room_busy:"))
        st = wait(40, arrived)
        last = st.get("last", "")
        if not arrived(st):
            entry["result"] = "timeout"
            results.append(entry); log("  %-40s timeout (status: %s)" % (room, last))
            continue
        if last.startswith(("room_unavailable:", "room_busy:")):
            entry["result"] = last.split(":")[0]
            results.append(entry); log("  %-40s %s" % (room, entry["result"]))
            continue
        entry["actual_room"] = st.get("room", "?")
        wait(settle)                                 # let uploads/fades finish
        vita.command("CAPTURE %d" % secs)
        # The status line is published every 15 frames: first see the capture
        # start, then wait for it to end (never move on in the middle of it).
        wait(10, lambda s: s.get("capture") == "1")
        t_end = time.time() + secs
        wait(secs + 15, lambda s: s.get("capture") == "0" and time.time() >= t_end)
        wait(1)
        entry["result"] = "ok"
        results.append(entry)
        log("  %-40s captured %ds (in %s)" % (room, secs, entry["actual_room"]))

    # download every new devlog
    after = vita.list()
    got = []
    for key, meta in sorted(after.items()):
        area, rel = key
        if area != "devlog" or key in before:
            continue
        data = vita.get(area, rel)
        dest = run_dir / "devlogs" / rel.replace("/", "_")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        got.append((rel, data.decode("utf-8", "replace")))
    live_file.close()

    # match devlogs to rooms by START_ROOM
    for rel, text in got:
        m = re.search(r"START_ROOM=(\S+)", text)
        start = m.group(1) if m else ""
        for e in results:
            if e["result"] == "ok" and e.get("actual_room") == start and not e["devlogs"]:
                e["devlogs"].append((rel, summarize_devlog(text)))
                break

    lines = ["# Auto-teste %s" % run_dir.name, "",
             "Plano: `%s`. Build: %s. Capítulo: %s." % (plan_path.name, status.get("build", "?"), status.get("chapter", "?")),
             "", "| Sala | Resultado | FPS médio | FPS mín | Quadro médio | Quadro máx | GML mais caro |",
             "|---|---|---|---|---|---|---|"]
    for e in results:
        if e["devlogs"]:
            s = e["devlogs"][0][1]
            top = ", ".join("%s %.1fms" % (n.replace("gml_Object_", "").replace("gml_Script_", ""), ms) for ms, n in s["gml_top"][:2])
            lines.append("| %s | ok | %.1f | %.1f | %.1f ms | %.1f ms | %s |" % (
                e["room"] if e.get("actual_room") in (None, e["room"]) else "%s → %s" % (e["room"], e["actual_room"]),
                s["fps_avg"], s["fps_min"], s["frame_ms_avg"], s["frame_ms_max"], top or "-"))
        else:
            lines.append("| %s | %s | | | | | |" % (e["room"], e["result"] or "sem devlog"))
    lines += ["", "Arquivos: `devlogs/` (capturas desta execução) e `probe-during-test.log` (log ao vivo durante o teste)."]
    report = run_dir / "report.md"
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    log("auto-test report: %s" % report)
    return report
