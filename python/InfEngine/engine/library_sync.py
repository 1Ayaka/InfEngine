"""Synchronize engine resources from the Python package into the project Library."""

import logging
import os
import shutil

_log = logging.getLogger("InfEngine.library_sync")

_SKIP = {"__pycache__", "__init__.py", "__init__.pyi", "icons.zip"}


def sync_resources(project_path: str) -> str:
    """Copy package resources into ``<project>/Library/Resources``.

    Performs a clean copy every launch so the Library always mirrors the
    current Python environment's built-in resources.

    Returns the Library resources directory path.
    """
    from InfEngine.resources import get_package_resources_path

    src = get_package_resources_path()
    dst = os.path.join(project_path, "Library", "Resources")

    if os.path.isdir(dst):
        shutil.rmtree(dst)

    shutil.copytree(
        src, dst,
        ignore=lambda _dir, entries: [e for e in entries if e in _SKIP],
    )

    _log.info("Synced engine resources → %s", dst)
    return dst
