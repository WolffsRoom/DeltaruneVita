# -*- mode: python ; coding: utf-8 -*-
import struct
from pathlib import Path

from PyInstaller.utils.win32.versioninfo import (
    FixedFileInfo, StringFileInfo, StringStruct, StringTable,
    VarFileInfo, VarStruct, VSVersionInfo,
)

root = Path(SPECPATH).parent

bits = struct.calcsize("P") * 8
arch_label = f"{bits}-bit"
exe_name = f"DeltaruneVitaPatcher_{bits}bit"

version_info = VSVersionInfo(
    ffi=FixedFileInfo(
        filevers=(0, 73, 0, 0),
        prodvers=(0, 73, 0, 0),
        mask=0x3F,
        flags=0x0,
        OS=0x40004,
        fileType=0x1,
        subtype=0x0,
        date=(0, 0),
    ),
    kids=[
        StringFileInfo([
            StringTable('040904B0', [
                StringStruct('CompanyName', 'WolffsRoom'),
                StringStruct('FileDescription', f'DELTARUNE PS Vita Patcher ({arch_label})'),
                StringStruct('FileVersion', '0.73.0.0'),
                StringStruct('InternalName', 'DeltaruneVitaPatcher'),
                StringStruct('LegalCopyright', 'WolffsRoom'),
                StringStruct('OriginalFilename', 'DeltaruneVitaPatcher.exe'),
                StringStruct('ProductName', 'Deltarune PS Vita Patcher'),
                StringStruct('ProductVersion', '0.73.0.0'),
                StringStruct('LegalCopyright', 'WolffsRoom'),
            ]),
        ]),
        VarFileInfo([VarStruct('Translation', [1033, 1200])]),
    ],
)

a = Analysis(['deltarune_vita_patcher.py'], pathex=[], binaries=[],
    datas=[(str(root / 'patch_data'), 'patch_data'), (str(root / 'webpatcher' / 'language_packs.json'), '.')],
    hiddenimports=['bsdiff4'], hookspath=[], hooksconfig={}, runtime_hooks=[],
    excludes=[], noarchive=False, optimize=0)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name='DeltaruneVitaPatcher',
    debug=False, bootloader_ignore_signals=False, strip=False, upx=True,
    console=True, disable_windowed_traceback=False, argv_emulation=False,
    target_arch=None, codesign_identity=None, entitlements_file=None,
    version=version_info,
    icon=str(root / 'icon.ico'))
