import subprocess

engine = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\src\butterscotch\build\Debug\butterscotch.exe"
data_win = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\local-test\data\deltarune\deltarunevita\chapter2\data.win"

events = [
    "gml_Script_scr_set_facing_sprites",
    "gml_Script_scr_caterpillar_add",
]

for ev in events:
    res = subprocess.run([engine, data_win, "--disassemble", ev], capture_output=True, text=True, errors="ignore")
    print(f"=== {ev} ===")
    print(res.stdout)
