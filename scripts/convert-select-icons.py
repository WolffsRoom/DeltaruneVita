from pathlib import Path

from PIL import Image


UI_DIR = Path(__file__).resolve().parents[1] / "data/prepared/deltarune/deltarunevita/ui"


for path in UI_DIR.glob("*.png"):
    source = Image.open(path).convert("RGBA")
    alpha = source.getchannel("A")
    indexed = source.convert("RGB").quantize(
        colors=255, method=Image.Quantize.MEDIANCUT
    )
    pixels = list(indexed.getdata())
    mask = list(alpha.getdata())
    indexed.putdata([255 if value == 0 else color for color, value in zip(pixels, mask)])
    palette = indexed.getpalette() or []
    indexed.putpalette((palette + [0] * (768 - len(palette)))[:768])
    indexed.save(path, optimize=True, transparency=255, bits=8)
    print(f"{path.name}: 8-bit indexed")
