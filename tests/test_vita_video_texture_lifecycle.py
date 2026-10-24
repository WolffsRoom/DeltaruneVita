from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "vita-probe" / "source" / "vita_video.c"


def test_video_placeholder_releases_vitagl_ownership_before_external_frame_binding():
    text = SOURCE.read_text(encoding="utf-8")
    marker = "vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));"
    pos = text.index(marker)
    tail = text[pos : pos + 520]
    assert "vglOverloadTexDataPointer(GL_TEXTURE_2D, NULL);" in tail, (
        "After freeing the 8x8 VitaGL placeholder, texture->data must be cleared "
        "before SceAvPlayer frame memory is attached; otherwise glDeleteTextures "
        "can release/dirty the stale pointer a second time."
    )
