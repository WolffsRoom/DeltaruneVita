#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/butterscotch/src/data_win.h"
SOURCE = ROOT / "src/butterscotch/src/data_win.c"


def require(text: str, needle: str, source: str) -> None:
    if needle not in text:
        raise AssertionError(f"{source}: missing {needle!r}")


def main() -> None:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    for symbol in (
        "DataWinRoomPayload",
        "DataWin_stageRoomPayload",
        "DataWin_adoptRoomPayload",
        "DataWin_freeStagedRoomPayload",
    ):
        require(header, symbol, "data_win.h")
        require(source, symbol, "data_win.c")

    stage = source[source.index("bool DataWin_stageRoomPayload"):source.index("bool DataWin_adoptRoomPayload")]
    require(stage, "fopen(dw->lazyLoadFilePath", "staging implementation")
    require(stage, "readRoomPayload", "staging implementation")
    if "dw->lazyLoadFile," in stage or "FILE* f = dw->lazyLoadFile" in stage:
        raise AssertionError("staging path uses the shared lazy-load FILE pointer")

    adopt = source[source.index("bool DataWin_adoptRoomPayload"):source.index("void DataWin_freeStagedRoomPayload")]
    require(adopt, "payload->roomIndex != roomIndex", "adoption implementation")
    require(adopt, "room->payloadLoaded", "adoption implementation")
    require(adopt, "payload->room.payloadLoaded = false", "adoption ownership transfer")

    sync = source[source.index("void DataWin_loadRoomPayload"):]
    require(sync, "DataWin_stageRoomPayload", "synchronous compatibility wrapper")
    require(sync, "DataWin_adoptRoomPayload", "synchronous compatibility wrapper")

    print("Detached room payload staging contract: PASS")


if __name__ == "__main__":
    main()
