from pathlib import Path


root = Path(__file__).resolve().parents[1]
renderer = (root / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")
header = (root / "src/butterscotch/src/gl/gl_renderer.h").read_text(encoding="utf-8")

assert "vitaModernReadBc3Pvr" in renderer
assert ".bc3.pvr" in renderer
assert "VITA_PVR_BC3_DXT5_FORMAT" in renderer
assert "GL_COMPRESSED_RGBA_S3TC_DXT5_EXT" in renderer
assert "glCompressedTexImage2D" in renderer
upload = renderer.index("glCompressedTexImage2D")
uploaded = renderer.index("modern_pvr_bc3_dxt5_uploaded", upload)
assert "glFinish()" not in renderer[upload:uploaded]

# Fonts, LUTs and textbox/UI pages must keep the lossless path.
assert "vitaModernPixelCriticalPage" in renderer
assert "textureFontPages" in renderer
assert "textureLookupPages" in renderer
assert '"spr_textbox"' in renderer

# Native profile remains RGBA4444; optimized and Flowery may use BC3.
assert "g_vitaTextureFormatProfile == 1" in renderer
assert "vitaChapter5FloweryBattleRoom" in renderer

# Eviction must account for the real compressed allocation rather than
# recomputing it as RGBA4444 from width and height.
assert "textureGpuBytes" in header
assert "gl->textureGpuBytes[pageId] = payloadSize" in renderer
assert "gl->textureGpuBytes[victim]" in renderer

print("Modern BC3 contracts: PASS")
