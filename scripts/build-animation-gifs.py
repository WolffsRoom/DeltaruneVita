#!/usr/bin/env python3
"""Build preview GIFs from the Vita loading-animation PNG folders."""

from pathlib import Path
import re

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data" / "frames" / "animations"
OUTPUT = ROOT / "data" / "frames" / "gifs_origem"

# Matches the frame timing used by src/vita-probe/source/playable_main.c.
DURATION_MS = {
    "toby_sleep": 375,
    "toby_dance": 125,
    "toby_walk": 180,
    "toby_spin": 125,
    "toby_turn": 100,
    "toby_car_load": 125,
    "toby_walk_load": 125,
    "loading_bullet": 125,
}


def frame_number(path: Path) -> tuple[int, str]:
    match = re.search(r"(\d+)(?!.*\d)", path.stem)
    return (int(match.group(1)) if match else 0, path.name.lower())


def gif_durations(frame_count: int, requested_ms: int) -> list[int]:
    """Represent half-centisecond timings without changing average playback speed."""
    lower = (requested_ms // 10) * 10
    upper = lower if lower == requested_ms else lower + 10
    if lower == upper:
        return [requested_ms] * frame_count
    return [lower if index % 2 == 0 else upper for index in range(frame_count)]


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    built = 0
    for folder in sorted((p for p in SOURCE.iterdir() if p.is_dir()), key=lambda p: p.name.lower()):
        paths = sorted(folder.glob("*.png"), key=frame_number)
        if not paths:
            continue

        frames = [Image.open(path).convert("RGBA") for path in paths]
        size = frames[0].size
        if any(frame.size != size for frame in frames):
            raise ValueError(f"{folder.name}: frames possuem dimensoes diferentes")

        destination = OUTPUT / f"{folder.name}.gif"
        requested_duration = DURATION_MS.get(folder.name, 125)
        frames[0].save(
            destination,
            save_all=True,
            append_images=frames[1:],
            duration=gif_durations(len(frames), requested_duration),
            loop=0,
            disposal=2,
            optimize=False,
        )
        for frame in frames:
            frame.close()
        built += 1
        print(
            f"{destination.name}: {len(paths)} frame(s), "
            f"{size[0]}x{size[1]}, {requested_duration} ms/frame (media)"
        )

    print(f"DONE gifs={built} output={OUTPUT}")


if __name__ == "__main__":
    main()
