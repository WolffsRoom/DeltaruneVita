import os
import sys
import numpy as np
from PIL import Image

def generate_texture_tests(target_page=56):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    data_win_path = os.path.join(project_root, "local-test", "data", "deltarune", "deltarunevita", "chapter5", "data.win")
    
    if not os.path.exists(data_win_path):
        print(f"data.win not found at {data_win_path}")
        return

    output_dir = os.path.join(project_root, "data", "texture-test")
    os.makedirs(output_dir, exist_ok=True)

    print(f"Reading TXTR chunk from {data_win_path}...")
    with open(data_win_path, "rb") as f:
        data = f.read()

    # Locate TXTR chunk
    txtr_pos = data.find(b"TXTR")
    if txtr_pos == -1:
        print("TXTR chunk not found!")
        return

    # Read TXTR count and offsets
    count = int.from_bytes(data[txtr_pos + 8:txtr_pos + 12], "little")
    print(f"Found {count} texture pages in TXTR chunk.")

    if target_page >= count:
        target_page = 0

    pointer_table_offset = txtr_pos + 12
    entry_offset = int.from_bytes(data[pointer_table_offset + target_page * 4:pointer_table_offset + target_page * 4 + 4], "little")
    
    r444_cache_path = os.path.join(project_root, "data", "texture-cache", "chapter5", f"page_{target_page:03d}.r444")
    img = None
    if os.path.exists(r444_cache_path):
        print(f"Loading cached R444 file: {r444_cache_path}")
        with open(r444_cache_path, "rb") as f:
            r444_data = f.read()
        
        # Header: VTC1 has magic 0x31435456 (20 bytes header: magic 4, w 4, h 4, stored 4, flags 4)
        # VTC4 has magic 0x34435456 (32 bytes header: magic 4, srcSize 4, srcOff 4, crc 4, w 4, h 4, stored 4, flags 4)
        import struct
        magic = struct.unpack("<I", r444_data[:4])[0]
        if magic == 0x31435456: # VTC1
            magic, srcSize, srcOff, w, h, storedSize, flags = struct.unpack("<IIIIIII", r444_data[:28])
            payload = r444_data[32:]
        else: # VTC4
            magic, srcSize, srcOff, srcCrc, w, h, storedSize, flags = struct.unpack("<IIIIIIII", r444_data[:32])
            payload = r444_data[32:]

        print(f"Header: magic=0x{magic:X}, w={w}, h={h}, stored={storedSize}, flags={flags}")
        
        if flags & 1:
            import zlib
            payload = zlib.decompress(payload)
            
        u16 = np.frombuffer(payload, dtype=np.uint16)
        if len(u16) > w * h:
            u16 = u16[:w * h]
        elif len(u16) < w * h:
            u16 = np.pad(u16, (0, w * h - len(u16)))
            
        r = ((u16 >> 12) & 0x0F) * 17
        g = ((u16 >> 8) & 0x0F) * 17
        b = ((u16 >> 4) & 0x0F) * 17
        a = (u16 & 0x0F) * 17
        arr_rgba = np.stack([r, g, b, a], axis=-1).astype(np.uint8).reshape((h, w, 4))
        img = Image.fromarray(arr_rgba, "RGBA")

    if img is None:
        print("Could not decode image!")
        return

    w, h = img.size
    print(f"Successfully loaded texture page {target_page}: {w}x{h}")

    arr = np.array(img, dtype=np.uint8) # RGBA8888

    # 1. ORIGINAL RGBA8888 (16.00 MB CDRAM)
    orig_path = os.path.join(output_dir, f"1_Original_RGBA8888_[16.00MB_CDRAM].png")
    img.save(orig_path)
    print(f"Saved: {orig_path}")

    # 2. RGBA4444 (8.00 MB CDRAM)
    # Scale 8-bit to 4-bit and back to 8-bit for PNG visualization
    r4 = (arr[:, :, 0] >> 4) << 4
    g4 = (arr[:, :, 1] >> 4) << 4
    b4 = (arr[:, :, 2] >> 4) << 4
    a4 = (arr[:, :, 3] >> 4) << 4
    arr_r4444 = np.stack([r4, g4, b4, a4], axis=-1).astype(np.uint8)
    r4444_img = Image.fromarray(arr_r4444, "RGBA")
    r4444_path = os.path.join(output_dir, f"2_RGBA4444_Cache_[8.00MB_CDRAM].png")
    r4444_img.save(r4444_path)
    print(f"Saved: {r4444_path}")

    # 3. PALETTIZED 8-BIT / INDEXED COLOR (4.00 MB CDRAM)
    # Quantize to 256 colors with alpha thresholding
    p_img = img.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
    # Convert quantized back to RGBA for PNG saving while maintaining index precision
    p_rgba = p_img.convert("RGBA")
    
    # Reapply exact alpha mask (0 or >128)
    p_arr = np.array(p_rgba, dtype=np.uint8)
    p_arr[:, :, 3] = np.where(arr[:, :, 3] < 128, 0, 255)
    pal_img = Image.fromarray(p_arr, "RGBA")
    pal_path = os.path.join(output_dir, f"3_Palettized_Indexed8_[4.00MB_CDRAM].png")
    pal_img.save(pal_path)
    print(f"Saved: {pal_path}")

    # 4. BC3 / DXT5 SIMULATION (4.00 MB CDRAM)
    # BC3 uses RGB 565 per 4x4 block + 4-bit alpha interpolated
    r5 = (arr[:, :, 0] >> 3) << 3
    g6 = (arr[:, :, 1] >> 2) << 2
    b5 = (arr[:, :, 2] >> 3) << 3
    a4_bc3 = (arr[:, :, 3] >> 4) << 4
    arr_bc3 = np.stack([r5, g6, b5, a4_bc3], axis=-1).astype(np.uint8)
    bc3_img = Image.fromarray(arr_bc3, "RGBA")
    bc3_path = os.path.join(output_dir, f"4_BC3_DXT5_[4.00MB_CDRAM].png")
    bc3_img.save(bc3_path)
    print(f"Saved: {bc3_path}")

    # 5. PVRTC 4BPP SIMULATION (2.00 MB CDRAM)
    # Low precision + color subsampling
    r4_pvr = (arr[:, :, 0] >> 4) << 4
    g4_pvr = (arr[:, :, 1] >> 4) << 4
    b4_pvr = (arr[:, :, 2] >> 4) << 4
    a3_pvr = (arr[:, :, 3] >> 5) << 5
    arr_pvr = np.stack([r4_pvr, g4_pvr, b4_pvr, a3_pvr], axis=-1).astype(np.uint8)
    pvr_img = Image.fromarray(arr_pvr, "RGBA")
    pvr_path = os.path.join(output_dir, f"5_PVRTC1_4BPP_[2.00MB_CDRAM].png")
    pvr_img.save(pvr_path)
    print(f"Saved: {pvr_path}")

    print("\nTexture comparison test successfully generated in data/texture-test!")

if __name__ == "__main__":
    generate_texture_tests(target_page=56)
