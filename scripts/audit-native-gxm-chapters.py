#!/usr/bin/env python3
"""Validate BC3 PVR texture inputs for all five GXM chapters.

A valid PVR only establishes an offline resource candidate, NOT real Vita
GPU residency nor shader compatibility.
"""
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/"data/prepared/deltarune/deltarunevita"
FMT="<IIQIIIIIIIII"
assert struct.calcsize(FMT)==52
grand_total=0
all_good=True
for chapter in range(1,6):
    directory=DATA/f"chapter{chapter}"/"pvr"
    files=sorted(directory.glob("page_*.bc3.pvr"))
    valid=0
    payload_size=0
    failures=[]
    for file in files:
        with file.open("rb") as f:
            header=f.read(52)
        if len(header)!=52:
            failures.append(file.name+":truncated")
            continue
        magic,flags,fmt,space,ctype,h,w,depth,surfaces,faces,mips,meta=struct.unpack(FMT,header)
        expected=((w+3)//4)*((h+3)//4)*16
        okay=(
            magic==0x03525650 and flags==0 and fmt==11 and
            space==1 and ctype==0 and
            depth==surfaces==faces==mips==1 and
            4<=w<=4096 and 4<=h<=4096 and
            w&(w-1)==0 and h&(h-1)==0 and
            meta<=65536 and
            file.stat().st_size==52+meta+expected
        )
        if not okay:
            failures.append(file.name+":invalid")
            continue
        valid+=1
        payload_size+=expected
    shaders=0
    data_file=DATA/f"chapter{chapter}"/"data.win"
    with data_file.open("rb") as data:
        data.seek(0,2)
        data_size=data.tell()
        offset=8
        while offset+8<=data_size:
            data.seek(offset)
            tag,size=struct.unpack("<4sI",data.read(8))
            if tag==b"SHDR":
                shaders=struct.unpack("<I",data.read(4))[0]
            offset+=8+size
            if offset>data_size:
                failures.append("data.win:invalid_chunk")
                break
    grand_total+=valid
    print(f"CHAPTER={chapter} bc3_pages={valid} shaders={shaders} "
          f"payload_mib={payload_size/1048576:.2f} errors={len(failures)}")
    if not valid or failures:
        all_good=False
        if failures:print("  Invalid:",", ".join(failures[:12]))
print(f"BC3_PVR_VALID={int(all_good)} TOTAL={grand_total}")
sys.exit(0 if all_good else 1)
