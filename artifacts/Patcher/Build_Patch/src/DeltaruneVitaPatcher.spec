# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path

root = Path(SPECPATH).parent
a = Analysis(['deltarune_vita_patcher.py'], pathex=[], binaries=[],
    datas=[(str(root / 'patch_data'), 'patch_data'), (str(root.parent.parent.parent / 'mods' / 'mods_list.txt'), '.')],
    hiddenimports=['bsdiff4'], hookspath=[], hooksconfig={}, runtime_hooks=[],
    excludes=[], noarchive=False, optimize=0)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name='DeltaruneVitaPatcher',
    debug=False, bootloader_ignore_signals=False, strip=False, upx=True,
    console=True, disable_windowed_traceback=False, argv_emulation=False,
    target_arch=None, codesign_identity=None, entitlements_file=None,
    icon=str(root / 'icon.ico'))
