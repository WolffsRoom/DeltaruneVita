#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/vita-probe/source/vita_startup_preload.h"
SOURCE = ROOT / "src/vita-probe/source/vita_startup_preload.c"
MAIN = ROOT / "src/vita-probe/source/playable_main.c"
CMAKE = ROOT / "src/vita-probe/CMakeLists.txt"


def require(text: str, needle: str, source: str) -> None:
    if needle not in text:
        raise AssertionError(f"{source}: missing {needle!r}")


def main() -> None:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    playable = MAIN.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")

    for symbol in (
        "VitaStartupPreload_begin",
        "VitaStartupPreload_join",
        "VitaStartupPreload_cancel",
        "VitaStartupPreloadStats",
    ):
        require(header, symbol, "vita_startup_preload.h")

    require(source, "VitaBackgroundWork_submit", "vita_startup_preload.c")
    require(source, "DataWin_parse", "vita_startup_preload.c")
    require(source, "progressCallback = NULL", "vita_startup_preload.c")
    require(source, "strcmp", "vita_startup_preload.c")
    require(playable, "STARTUP_PRELOAD=begin", "playable_main.c")
    require(playable, "STARTUP_PRELOAD=adopted", "playable_main.c")
    require(playable, "STARTUP_PRELOAD=fallback", "playable_main.c")
    require(cmake, "source/vita_startup_preload.c", "CMakeLists.txt")

    video_pos = playable.index("first_load_video_play(firstLoadGain)")
    begin_pos = playable.index("VitaStartupPreload_begin")
    parse_pos = playable.index("DataWin_parse(game_path, options)")
    assert begin_pos < video_pos < parse_pos

    worker_body = source[source.index("static bool startupParseJob"):]
    for forbidden in ("draw_loading_screen", "vgl", "glBind", "VM_create"):
        if forbidden in worker_body:
            raise AssertionError(f"startup worker contains forbidden symbol {forbidden!r}")

    print("Chapter 0 startup preload contract: PASS")


if __name__ == "__main__":
    main()
