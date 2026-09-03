from pathlib import Path

patch_root = Path(r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\artifacts\Patcher\Build_Patch\patch_data")

emb_size = sum(p.stat().st_size for p in (patch_root / "embedded").rglob("*") if p.is_file()) if (patch_root / "embedded").exists() else 0
patch_size = sum(p.stat().st_size for p in (patch_root / "patches").rglob("*") if p.is_file()) if (patch_root / "patches").exists() else 0

print(f"Embedded files size: {emb_size / (1024*1024):.2f} MB")
print(f"Patches (bsdiff) size: {patch_size / (1024*1024):.2f} MB")
print(f"Total patch_data size: {(emb_size + patch_size) / (1024*1024):.2f} MB")
