import os
import subprocess
from PIL import Image

VITA_SIZE = (960, 544)

def save_vita_rgba_png(source_path, output_path):
    """Store a full-colour Vita-sized source for BC3 and PNG fallback."""
    with Image.open(source_path) as image:
        rgba = image.convert("RGBA").resize(VITA_SIZE, Image.Resampling.LANCZOS)
        rgba.save(output_path, "PNG", optimize=True, compress_level=9)

def convert_pvr(pvr_tool, png_path, pvr_path):
    if not os.path.exists(pvr_tool):
        return False
    cmd = [pvr_tool, "-ics", "sRGB", "-f", "BC3,UB,sRGB",
           "-i", png_path, "-o", pvr_path]
    result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return result.returncode == 0 and os.path.exists(pvr_path)

def convert_ps4_borders():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    ps4_dir = os.path.join(project_root, "data", "Deltarune Borders (Console Version)")
    output_dir = os.path.join(project_root, "data", "prepared", "deltarune", "deltarunevita", "borders")
    
    pvr_tool = r"C:\Program Files\Imgtec\PowerVR_Tools\PVRTexTool\CLI\Windows_x86_64\PVRTexToolCLI.exe"
    
    if not os.path.exists(ps4_dir):
        print(f"PS4 borders directory not found: {ps4_dir}")
        return

    os.makedirs(output_dir, exist_ok=True)

    png_files = [f for f in os.listdir(ps4_dir) if f.endswith(".png")]
    print(f"Found {len(png_files)} PS4 border PNGs. Processing for PS Vita (960x544 -> BC3 PVR)...")

    count = 0
    for fname in png_files:
        src_path = os.path.join(ps4_dir, fname)
        out_png = os.path.join(output_dir, fname)
        out_pvr = os.path.join(output_dir, fname[:-4] + ".pvr")

        # Keep the full RGBA colour source. The old pipeline quantized to a
        # 256-colour PNG before BC3 encoding, permanently discarding gradients
        # and fine colour detail even though BC3 did not require a palette.
        save_vita_rgba_png(src_path, out_png)

        # Convert 960x544 PNG to 960x544 BC3 PVR
        if convert_pvr(pvr_tool, out_png, out_pvr):
            count += 1

    # Optional higher-detail Chapter 1 pack follows the same source/output
    # contract as the official borders. Always rebuild from mods/Borders;
    # re-encoding the already prepared PNG made repeated runs cumulative and
    # could preserve an older indexed/quantized result.
    mod_source_dir = os.path.join(project_root, "mods", "Borders")
    mod_dir = os.path.join(project_root, "data", "prepared", "deltarune",
                           "deltarunevita", "mods", "Borders")
    mod_count = 0
    if os.path.isdir(mod_source_dir):
        os.makedirs(mod_dir, exist_ok=True)
        for fname in ("border1.png", "border2.png", "border3.png"):
            source_path = os.path.join(mod_source_dir, fname)
            if not os.path.exists(source_path):
                continue
            png_path = os.path.join(mod_dir, fname)
            save_vita_rgba_png(source_path, png_path)
            if convert_pvr(pvr_tool, png_path,
                           os.path.join(mod_dir, fname[:-4] + ".pvr")):
                mod_count += 1

    print(f"\nGenerated {len(png_files)} full-colour PNG borders (960x544), "
          f"{count} standard BC3 PVRs and {mod_count} mod BC3 PVRs.")

if __name__ == "__main__":
    convert_ps4_borders()
