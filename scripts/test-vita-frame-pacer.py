#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/vita-probe/source/vita_frame_pacer.h"
SOURCE = ROOT / "src/vita-probe/source/vita_frame_pacer.c"
MAIN = ROOT / "src/vita-probe/source/playable_main.c"
CMAKE = ROOT / "src/vita-probe/CMakeLists.txt"


def deadlines(fps: int, count: int) -> list[int]:
    quotient, remainder = divmod(1_000_000, fps)
    now = 0
    carried = 0
    result = []
    for _ in range(count):
        now += quotient
        carried += remainder
        if carried >= fps:
            now += carried // fps
            carried %= fps
        result.append(now)
    return result


def require(text: str, needle: str, source: str) -> None:
    if needle not in text:
        raise AssertionError(f"{source}: missing {needle!r}")


def main() -> None:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    playable = MAIN.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")

    for symbol in (
        "VitaFramePacer_init",
        "VitaFramePacer_setRate",
        "VitaFramePacer_plan",
        "VitaFramePacer_noteSleep",
        "VitaFramePacer_finish",
        "VitaFramePacer_reset",
    ):
        require(header, symbol, "vita_frame_pacer.h")

    require(source, "periodRemainder", "vita_frame_pacer.c")
    require(source, "remainderCarry", "vita_frame_pacer.c")
    require(source, "VITA_FRAME_PACER_MAX_GUARD_US", "vita_frame_pacer.c")
    require(playable, "VitaFramePacer_plan", "playable_main.c")
    require(playable, "VitaFramePacer_reset", "playable_main.c")
    require(cmake, "source/vita_frame_pacer.c", "CMakeLists.txt")

    for fps in (30, 40, 60):
        result = deadlines(fps, fps * 10)
        assert result[-1] == 10_000_000, (fps, result[-1])
        assert all(b > a for a, b in zip(result, result[1:]))
    assert deadlines(40, 3) == [25_000, 50_000, 75_000]
    assert deadlines(60, 3) == [16_666, 33_333, 50_000]

    print("Vita frame pacer contract: PASS")


if __name__ == "__main__":
    main()
