from pathlib import Path


root = Path(__file__).resolve().parents[1]
renderer = (root / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")

start = renderer.index("void GLRenderer_trimTextureCacheForRoomChange")
end = renderer.index("bool GLRenderer_ensureTextureLoaded", start)
trim = renderer[start:end]

# A requested room-change trim is a safety boundary. It must not be skipped
# merely because VRAM has room while the RAM pool is nearly exhausted.
assert "vitaTextureUploadHasHeadroom" not in trim

# Prepared RGBA4444 pages are CPU-side staging allocations. Room Navigator can
# cross unrelated rooms repeatedly, so stale entries must be released at the
# same explicit trim boundary rather than accumulating alongside the new room.
assert "cpuTextureCachePixels" in trim
assert "free(gl->cpuTextureCachePixels" in trim
assert "cpuTextureCachePage" in trim

print("Modern room-jump memory contracts: PASS")
