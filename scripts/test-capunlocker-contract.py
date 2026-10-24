#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
THREAD_POLICY_C = ROOT / "src/vita-probe/source/vita_thread_policy.c"
THREAD_POLICY_H = ROOT / "src/vita-probe/source/vita_thread_policy.h"
PLAYABLE_MAIN = ROOT / "src/vita-probe/source/playable_main.c"
VITA_VIDEO = ROOT / "src/vita-probe/source/vita_video.c"
CMAKE = ROOT / "src/vita-probe/CMakeLists.txt"


def require(text: str, needle: str, source: str) -> None:
    if needle not in text:
        raise AssertionError(f"{source}: missing {needle!r}")


def main() -> None:
    policy_c = THREAD_POLICY_C.read_text(encoding="utf-8")
    policy_h = THREAD_POLICY_H.read_text(encoding="utf-8")
    playable = PLAYABLE_MAIN.read_text(encoding="utf-8")
    video = VITA_VIDEO.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")

    require(policy_h, "VitaThread_createBackground", "vita_thread_policy.h")
    require(policy_c, "VITA_CPU_MASK_UNLOCKED_CORE", "vita_thread_policy.c")
    require(policy_c, "sceKernelCreateThread", "vita_thread_policy.c")
    require(policy_c, "fallback", "vita_thread_policy.c")
    require(cmake, "source/vita_thread_policy.c", "CMakeLists.txt")

    require(playable, "VitaThread_createBackground", "playable_main.c")
    require(playable, "CAPUNLOCKER=texture_generator", "playable_main.c")
    require(playable, "CAPUNLOCKER=first_load_decoder", "playable_main.c")
    require(video, "VitaThread_createBackground", "vita_video.c")
    require(video, "CAPUNLOCKER=video_frame_decoder", "vita_video.c")

    print("CapUnlocker optional fourth-core contract: PASS")


if __name__ == "__main__":
    main()
