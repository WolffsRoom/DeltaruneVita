import subprocess

engine = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\src\butterscotch\build\Debug\butterscotch.exe"
data_win = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\local-test\data\deltarune\deltarunevita\chapter2\data.win"

res = subprocess.run([engine, data_win, "--disassemble", "gml_Object_obj_caterpillarchara_Create_0"], capture_output=True, text=True, errors="ignore")
print("=== gml_Object_obj_caterpillarchara_Create_0 ===")
print(res.stdout)
