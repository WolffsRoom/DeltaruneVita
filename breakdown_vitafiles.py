from pathlib import Path
from collections import defaultdict

dev_root = Path(r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\artifacts\Patcher\Build_Patch\VitaFiles")

subfolder_sizes = defaultdict(int)
for p in dev_root.rglob("*"):
    if p.is_file():
        parts = p.relative_to(dev_root).parts
        if len(parts) >= 2:
            sub = "/".join(parts[:3]) if len(parts) >= 3 else "/".join(parts[:2])
            subfolder_sizes[sub] += p.stat().st_size

print("=== Folder Size Breakdown in VitaFiles ===")
for sub, sz in sorted(subfolder_sizes.items(), key=lambda x: x[1], reverse=True):
    print(f"  {sz / (1024*1024):7.2f} MB : {sub}")
