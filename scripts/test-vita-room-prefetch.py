#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/vita-probe/source/vita_room_prefetch.h"
SOURCE = ROOT / "src/vita-probe/source/vita_room_prefetch.c"
RUNNER = ROOT / "src/butterscotch/src/runner.c"
MAIN = ROOT / "src/vita-probe/source/playable_main.c"
CMAKE = ROOT / "src/vita-probe/CMakeLists.txt"


def require(text: str, needle: str, source: str) -> None:
    if needle not in text:
        raise AssertionError(f"{source}: missing {needle!r}")


def main() -> None:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    runner = RUNNER.read_text(encoding="utf-8")
    playable = MAIN.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")

    for symbol in (
        "VitaRoomPrefetch_init",
        "VitaRoomPrefetch_prepare",
        "VitaRoomPrefetch_cancel",
        "VitaRoomPrefetch_shutdown",
    ):
        require(header, symbol, "vita_room_prefetch.h")
        require(source, symbol, "vita_room_prefetch.c")

    require(source, "DataWin_stageRoomPayload", "vita_room_prefetch.c")
    require(source, "DataWin_adoptRoomPayload", "vita_room_prefetch.c")
    require(source, "VitaBackgroundWork_submit", "vita_room_prefetch.c")
    require(source, "VitaBackgroundWork_cancelGeneration", "vita_room_prefetch.c")
    require(runner, "VitaRoomPrefetch_prepare", "runner.c")
    require(runner, "vitaRoomTransitionWaiting", "runner.c")
    require(playable, "VitaRoomPrefetch_init", "playable_main.c")
    require(playable, "VitaRoomPrefetch_shutdown", "playable_main.c")
    require(cmake, "source/vita_room_prefetch.c", "CMakeLists.txt")

    worker = source[source.index("static bool roomPayloadJob"):source.index("void VitaRoomPrefetch_init")]
    for forbidden in ("glBind", "vgl", "Runner_executeEvent", "VM_execute"):
        if forbidden in worker:
            raise AssertionError(f"room worker contains forbidden symbol {forbidden!r}")

    print("Vita room prefetch contract: PASS")


if __name__ == "__main__":
    main()
