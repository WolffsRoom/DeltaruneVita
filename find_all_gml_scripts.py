import re
from pathlib import Path

p = Path(r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\local-test\data\deltarune\deltarunevita\chapter1\data.win")
data = p.read_bytes()

# Search for gml_Script_ or gml_Object_
scripts = set(re.findall(b"gml_[A-Za-z0-9_]+", data))
for s in sorted(scripts):
    s_str = s.decode("utf-8")
    if any(k in s_str.lower() for k in ["select", "chapter", "place", "title", "version", "menu"]):
        print(s_str)
