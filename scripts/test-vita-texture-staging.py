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
        "DataWinStagedTexture",
        "DataWin_stageTexture",
        "DataWin_adoptStagedTexture",
        "DataWin_freeStagedTexture",
    ):
        require(header, symbol, "data_win.h")
        require(source, symbol, "data_win.c")

    stage = source[source.index("bool DataWin_stageTexture"):source.index("bool DataWin_adoptStagedTexture")]
    require(stage, "fopen(dw->lazyLoadFilePath", "texture staging")
    require(stage, "maxBytes", "texture staging budget")
    require(stage, "fread", "texture staging read")
    for forbidden in ("glTexImage", "glCompressedTexImage", "glBindTexture", "vgl"):
        if forbidden in stage:
            raise AssertionError(f"CPU texture stage contains graphics call {forbidden!r}")

    adopt = source[source.index("bool DataWin_adoptStagedTexture"):source.index("void DataWin_freeStagedTexture")]
    require(adopt, "textureId != staged->textureId", "texture adoption identity")
    require(adopt, "texture->blobData != nullptr", "texture adoption collision guard")
    require(adopt, "staged->bytes = nullptr", "texture adoption ownership transfer")

    print("Vita texture CPU staging contract: PASS")


if __name__ == "__main__":
    main()
