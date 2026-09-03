import subprocess

engine = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\src\butterscotch\build\Debug\butterscotch.exe"
data_win = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\local-test\data\deltarune\deltarunevita\chapter1\data.win"

res = subprocess.run([engine, data_win, "--list-objects"], capture_output=True, text=True, errors="ignore")
print(res.stdout[:5000])
