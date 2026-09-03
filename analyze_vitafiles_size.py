from pathlib import Path
import os

dev_root = Path(r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\artifacts\Patcher\Build_Patch\VitaFiles")
steam_root = Path(r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\artifacts\Patcher\Build_Patch\SteamFiles")

print("=== VitaFiles Top Directories & Files by Size ===")
files_with_size = []
total_vita_size = 0
for p in dev_root.rglob("*"):
    if p.is_file():
        sz = p.stat().st_size
        total_vita_size += sz
        files_with_size.append((sz, p.relative_to(dev_root)))

files_with_size.sort(reverse=True)

print(f"Total VitaFiles size: {total_vita_size / (1024*1024):.2f} MB")
print("\nTop 30 largest files in VitaFiles:")
for sz, rel in files_with_size[:30]:
    print(f"  {sz / (1024*1024):7.2f} MB : {rel}")

print("\n=== SteamFiles Top Directories & Files by Size ===")
steam_files_with_size = []
total_steam_size = 0
if steam_root.exists():
    for p in steam_root.rglob("*"):
        if p.is_file():
            sz = p.stat().st_size
            total_steam_size += sz
            steam_files_with_size.append((sz, p.relative_to(steam_root)))

    steam_files_with_size.sort(reverse=True)
    print(f"Total SteamFiles size: {total_steam_size / (1024*1024):.2f} MB")
    print("\nTop 30 largest files in SteamFiles:")
    for sz, rel in steam_files_with_size[:30]:
        print(f"  {sz / (1024*1024):7.2f} MB : {rel}")
