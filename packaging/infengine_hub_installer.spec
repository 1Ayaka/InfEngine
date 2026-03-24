# -*- mode: python ; coding: utf-8 -*-

import os

block_cipher = None

_PACKAGING_DIR = os.path.dirname(os.path.abspath(SPEC))
_ROOT_DIR = os.path.dirname(_PACKAGING_DIR)
_PAYLOAD_DIR = os.path.join(_ROOT_DIR, "dist", "InfEngine Hub")
_OPTIONAL_RUNTIME_DIR = os.path.join(_PACKAGING_DIR, "runtime")

# Locate OpenSSL DLLs required by _ssl.pyd — PyInstaller often misses these in conda envs.
import sys as _sys
_env_lib_bin = os.path.join(os.path.dirname(_sys.executable), "Library", "bin")
_ssl_binaries = []
for _dll_name in ("libssl-3-x64.dll", "libcrypto-3-x64.dll", "libssl-3.dll", "libcrypto-3.dll"):
    _p = os.path.join(_env_lib_bin, _dll_name)
    if os.path.isfile(_p):
        _ssl_binaries.append((_p, "."))

if not os.path.isdir(_PAYLOAD_DIR):
    raise SystemExit(f"Hub payload not found: {_PAYLOAD_DIR}")


def collect_tree(src_root: str, dest_root: str):
    datas = []
    for root, _dirs, files in os.walk(src_root):
        rel_dir = os.path.relpath(root, src_root)
        target_dir = dest_root if rel_dir == "." else os.path.join(dest_root, rel_dir)
        for filename in files:
            datas.append((os.path.join(root, filename), target_dir))
    return datas


def collect_optional_tree(src_root: str, dest_root: str):
    if not os.path.isdir(src_root):
        return []
    return collect_tree(src_root, dest_root)


a = Analysis(
    [os.path.join(_PACKAGING_DIR, "installer_gui.py")],
    pathex=[_PACKAGING_DIR],
    binaries=[*_ssl_binaries],
    datas=[
        (os.path.join(_PACKAGING_DIR, "resources", "icon.png"), "resources"),
        (os.path.join(_PACKAGING_DIR, "resources", "PingFangTC-Regular.otf"), "resources"),
        *collect_tree(_PAYLOAD_DIR, "payload"),
        *collect_optional_tree(_OPTIONAL_RUNTIME_DIR, os.path.join("payload", "InfEngineHubData", "runtime")),
    ],
    hiddenimports=[
        "installer",
        "installer.install_python_runtime",
        "ssl",
        "_ssl",
        "_hashlib",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="InfEngineHubInstaller",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=os.path.join(_PACKAGING_DIR, "resources", "icon.png"),
    uac_admin=True,
)