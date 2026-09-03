from pathlib import Path

patcher_dir = Path(r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\artifacts\Patcher")

print("=== Folders and Files in artifacts/Patcher ===")
for p in sorted(patcher_dir.iterdir()):
    if p.is_dir():
        print(f"[DIR]  {p.name}")
        for sub in sorted(p.iterdir()):
            print(f"       - {sub.name}")
    else:
        print(f"[FILE] {p.name}")
