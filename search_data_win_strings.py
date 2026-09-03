from pathlib import Path
import re

for folder in ["chapter0", "launcher"]:
    p = Path(rf"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\local-test\data\deltarune\deltarunevita\{folder}\data.win")
    if p.exists():
        data = p.read_bytes()
        print(f"=== {folder} data.win size: {len(data)} ===")
        matches = re.findall(b"PLACE_[A-Za-z0-9_]*", data)
        print("Matches PLACE_:", set(matches[:20]))
        matches2 = re.findall(b"Toby[^\x00]*", data)
        print("Matches Toby:", set(matches2))
        matches3 = re.findall(b"v[0-9]+[^\x00]*", data)
        print("Matches v[0-9]:", set(matches3[:20]))
