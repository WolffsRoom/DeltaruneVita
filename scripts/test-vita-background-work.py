#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/vita-probe/source/vita_background_work.h"
SOURCE = ROOT / "src/vita-probe/source/vita_background_work.c"
CMAKE = ROOT / "src/vita-probe/CMakeLists.txt"


def require(text: str, needle: str, source: str) -> None:
    if needle not in text:
        raise AssertionError(f"{source}: missing {needle!r}")


def model_accept(budget: int, reserved: int, requested: int) -> bool:
    return requested <= budget and reserved <= budget - requested


def model_stale(active_generation: int, result_generation: int) -> bool:
    return active_generation != result_generation


def main() -> None:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")

    for symbol in (
        "VitaBackgroundWork_create",
        "VitaBackgroundWork_submit",
        "VitaBackgroundWork_poll",
        "VitaBackgroundWork_cancelGeneration",
        "VitaBackgroundWork_destroy",
        "VITA_BACKGROUND_DEFAULT_BUDGET",
        "VitaBackgroundJobState",
    ):
        require(header, symbol, "vita_background_work.h")

    require(source, "VitaThread_createBackground", "vita_background_work.c")
    require(source, "sceKernelCreateSema", "vita_background_work.c")
    require(source, "VITA_BACKGROUND_JOB_CANCELLED", "vita_background_work.c")
    require(source, "reservedBytes", "vita_background_work.c")
    require(source, "generation", "vita_background_work.c")
    require(cmake, "source/vita_background_work.c", "CMakeLists.txt")

    forbidden = ("glTexImage", "glBindTexture", "vgl", "Runner_executeEvent", "VM_execute")
    for symbol in forbidden:
        if symbol in source:
            raise AssertionError(f"worker source contains forbidden graphics/VM symbol {symbol!r}")

    mib = 1024 * 1024
    assert model_accept(8 * mib, 0, 8 * mib)
    assert model_accept(8 * mib, 3 * mib, 5 * mib)
    assert not model_accept(8 * mib, 3 * mib, 5 * mib + 1)
    assert not model_accept(8 * mib, 0, 8 * mib + 1)
    assert not model_stale(7, 7)
    assert model_stale(8, 7)

    print("Vita background work contract: PASS")


if __name__ == "__main__":
    main()
