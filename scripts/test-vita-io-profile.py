#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/vita-probe/source/vita_io_profile.h"
SOURCE = ROOT / "src/vita-probe/source/vita_io_profile.c"
STARTUP = ROOT / "src/vita-probe/source/vita_startup_preload.c"
ROOM = ROOT / "src/vita-probe/source/vita_room_prefetch.c"
CMAKE = ROOT / "src/vita-probe/CMakeLists.txt"
DOC = ROOT / "docs/iostaging-v078-test-protocol.md"


def require(text: str, needle: str, source: str) -> None:
    if needle not in text:
        raise AssertionError(f"{source}: missing {needle!r}")


def main() -> None:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    startup = STARTUP.read_text(encoding="utf-8")
    room = ROOM.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")
    doc = DOC.read_text(encoding="utf-8")

    for symbol in ("VitaIoProfile_begin", "VitaIoProfile_end", "VitaIoProfile_format"):
        require(header, symbol, "vita_io_profile.h")
        require(source, symbol, "vita_io_profile.c")
    for field in ("operation=", "bytes=", "path=", "queue_us=", "read_us=", "decode_us=", "overlap=", "success=", "throughput_kib_s="):
        require(source, field, "IO_PROFILE format")
    require(startup, "VitaIoProfile_begin", "startup preload telemetry")
    require(room, "VitaIoProfile_begin", "room prefetch telemetry")
    require(cmake, "source/vita_io_profile.c", "CMakeLists.txt")
    require(doc, "IOStaging desativado", "IOStaging protocol")
    require(doc, "IOStaging ativado", "IOStaging protocol")
    require(doc, "tres execucoes", "IOStaging protocol")
    if "iostaging.cfg" in source.lower():
        raise AssertionError("runtime must not infer whether IOStaging loaded from a config file")

    print("Vita IO profiling contract: PASS")


if __name__ == "__main__":
    main()
