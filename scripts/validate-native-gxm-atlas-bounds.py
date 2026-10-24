#!/usr/bin/env python3
"""Cross-check all GameMaker TPAG atlas rectangles against native GXM PVR pages.

Unlike basic PVR header tests, this catches a syntactically valid but wrong
image/locale asset whose width/height are too small for the source sprites.
"""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
PREPARED = ROOT / "data/prepared/deltarune/deltarunevita"

def chunks(f, length):
    offset=8
    while offset+8<=length:
        f.seek(offset)
        header=f.read(8)
        if len(header)!=8:
            break
        tag, size=struct.unpack("<4sI",header)
        start=offset+8
        if start+size>length:
            raise ValueError(f"{tag!r}: chunk exceeds file")
        yield tag,start,size
        offset=start+size

def audit(chapter):
    root=PREPARED / f"chapter{chapter}"
    f=(root/"data.win").open("rb")
    length=(root/"data.win").stat().st_size
    items=0
    bypage={}
    totaltex=0
    for tag,start,size in chunks(f,length):
        if tag not in (b"TPAG",b"TXTR"):
            continue
        f.seek(start)
        count=struct.unpack("<I",f.read(4))[0]
        if count>1000000:
            raise ValueError("absurd entry count")
        offsets=struct.unpack(f"<{count}I",f.read(count*4))
        if tag==b"TXTR":
            totaltex=count
        else:
            for i,ptr in enumerate(offsets):
                if ptr==0:
                    continue
                if ptr+22>length:
                    raise ValueError(f"TPAG[{i}] pointer out of range")
                f.seek(ptr)
                sx,sy,sw,sh,*_,page=struct.unpack("<10Hh",f.read(22))
                if page<0 or sw==0 or sh==0:
                    continue
                prev=bypage.get(page,(0,0,0))
                bypage[page]=(max(prev[0],sx+sw),max(prev[1],sy+sh),prev[2]+1)
                items+=1
    f.close()
    missing=[]
    small=[]
    malformed=[]
    valid=0
    for page in range(totaltex):
        path=root/"pvr"/f"page_{page:03d}.bc3.pvr"
        if not path.exists():
            missing.append((page,bypage.get(page,(0,0,0))[2]))
            continue
        with path.open("rb") as handle:
            header=handle.read(52)
        if len(header)!=52:
            malformed.append((page,"header"))
            continue
        magic,flags,fmt,color,channel,h,w,depth,surfaces,faces,mips,meta=struct.unpack("<IIQIIIIIIIII",header)
        reqw,reqh,refs=bypage.get(page,(0,0,0))
        if w<reqw or h<reqh:
            small.append((page,(w,h),(reqw,reqh),refs))
        expected=52+meta+((w+3)//4)*((h+3)//4)*16
        if not(magic==0x03525650 and flags==0 and fmt==11 and
               color==1 and channel==0 and depth==1 and surfaces==1 and
               faces==1 and mips==1 and meta<=65536 and
               path.stat().st_size==expected):
            malformed.append((page,"format_or_length"))
        else:
            valid+=1
    print(f"CHAPTER={chapter} txtr={totaltex} tpag_refs={items} "
          f"bc3_valid={valid} missing={len(missing)} "
          f"undersized={len(small)} invalid={len(malformed)}")
    for key,values in [("MISSING",missing),("ATLAS_BOUNDS",small),("INVALID",malformed)]:
        for value in values[:15]:
            print(f"  {key} {value}")
    return not(missing or small or malformed)

if __name__=="__main__":
    ok=all([audit(ch) for ch in range(6)])
    print("NATIVE_GXM_ATLAS_BOUNDS="+("PASS" if ok else "FAIL"))
    sys.exit(0 if ok else 1)
