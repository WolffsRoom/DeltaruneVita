import subprocess

engine = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\src\butterscotch\build\Debug\butterscotch.exe"
data_win = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\local-test\data\deltarune\deltarunevita\chapter1\data.win"

res = subprocess.run([engine, data_win, "--disassemble", "gml_Object_obj_place_chapter_select_Create_0"], capture_output=True, text=True, errors="ignore")
print("=== gml_Object_obj_place_chapter_select_Create_0 ===")
print(res.stdout)

res2 = subprocess.run([engine, data_win, "--disassemble", "gml_Object_obj_place_chapter_select_Draw_0"], capture_output=True, text=True, errors="ignore")
print("=== gml_Object_obj_place_chapter_select_Draw_0 ===")
print(res2.stdout)
