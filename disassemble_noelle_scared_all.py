import subprocess

engine = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\src\butterscotch\build\Debug\butterscotch.exe"
data_win = r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\local-test\data\deltarune\deltarunevita\chapter2\data.win"

events = [
    "gml_Object_obj_noelle_scared_Alarm_0",
    "gml_Object_obj_noelle_scared_Step_0",
]

for ev in events:
    res = subprocess.run([engine, data_win, "--disassemble", ev], capture_output=True, text=True, errors="ignore")
    print(f"=== {ev} ===")
    print(res.stdout)
