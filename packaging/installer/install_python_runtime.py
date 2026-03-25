from __future__ import annotations

import argparse
import ctypes
import os
import sys


_PACKAGING_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PACKAGING_DIR not in sys.path:
    sys.path.insert(0, _PACKAGING_DIR)

from embed_runtime_manager import PythonRuntimeManager


def _show_message_box(title: str, message: str, icon: int = 0x40) -> None:
    if sys.platform != "win32":
        return
    try:
        ctypes.windll.user32.MessageBoxW(None, message, title, icon)
    except Exception:
        pass


def install_runtime_for_app(app_dir: str, progress_callback=None) -> str:
    bundle_runtime_dir = os.path.join(app_dir, "InfEngineHubData", "runtime")
    manager = PythonRuntimeManager(bundle_runtime_dir=bundle_runtime_dir)
    return manager.ensure_runtime(on_status=progress_callback, allow_frozen_repair=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app-dir")
    args = parser.parse_args()

    if not args.app_dir:
        _show_message_box(
            "InfEngine Runtime Installer",
            "This program is an internal installer helper for InfEngine Hub.\n\n"
            "Please run InfEngineHubInstaller.exe instead of launching this file directly.",
            0x30,
        )
        return 1

    install_runtime_for_app(args.app_dir)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        _show_message_box(
            "InfEngine Runtime Installer Error",
            str(exc),
            0x10,
        )
        try:
            sys.stderr.write(str(exc) + "\n")
        except Exception:
            pass
        raise SystemExit(1)
