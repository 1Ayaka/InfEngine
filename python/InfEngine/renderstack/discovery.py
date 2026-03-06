"""
Pipeline and Pass Auto-Discovery

Scans the project for RenderPipeline subclasses and RenderPass subclasses,
providing registration dictionaries for the Editor UI.

Discovery strategies:
    - ``discover_pipelines()``: scans ``__subclasses__()`` recursively,
      after importing any user scripts that reference ``RenderPipeline``
    - ``discover_passes()``: scans ``RenderPass.__subclasses__()`` recursively,
      after importing any user scripts that reference ``RenderPass``
    - Both exclude abstract bases and internal classes (prefixed with ``_``)
"""

from __future__ import annotations

import importlib.util
import os
import sys
from typing import Dict, Set, Type


def discover_pipelines() -> Dict[str, type]:
    """Scan all loaded RenderPipeline subclasses.

    Also scans user project scripts for ``RenderPipeline`` references and
    imports them so that ``__subclasses__()`` can find them.

    Returns:
        ``{pipeline.name: pipeline_class}`` dictionary.
        Excludes classes whose ``name`` starts with ``"_"``.
    """
    from InfEngine.rendering.render_pipeline import RenderPipeline

    _ensure_user_scripts_loaded("RenderPipeline")

    result: Dict[str, type] = {}
    _collect_subclasses(RenderPipeline, result, name_attr="name")
    return result


def discover_passes() -> Dict[str, type]:
    """Scan all loaded RenderPass subclasses.

    Also scans user project scripts for ``RenderPass`` (or subclass)
    references and imports them.

    Returns:
        ``{pass.name: pass_class}`` dictionary.
        Excludes abstract bases (GeometryPass)
        and classes whose ``name`` is empty or starts with ``"_"``.
    """
    from InfEngine.renderstack.render_pass import RenderPass

    _ensure_user_scripts_loaded("RenderPass", "GeometryPass", "FullScreenEffect")

    result: Dict[str, type] = {}
    _collect_subclasses(RenderPass, result, name_attr="name")
    return result


# -- Internal helpers -------------------------------------------------------

_loaded_scripts: Set[str] = set()
_loaded_script_modules: Dict[str, str] = {}
_loaded_script_mtime: Dict[str, float] = {}


def _ensure_user_scripts_loaded(*keywords: str) -> None:
    """Scan the project root for ``.py`` files containing any of *keywords*
    and import them so their classes appear in ``__subclasses__()``.

    Each file is imported at most once across the lifetime of the process.
    """
    from InfEngine.engine.project_context import get_project_root

    project_root = get_project_root()
    if not project_root or not os.path.isdir(project_root):
        return

    _prune_deleted_loaded_scripts()

    for dirpath, _dirs, filenames in os.walk(project_root):
        # Skip hidden dirs and __pycache__
        rel = os.path.relpath(dirpath, project_root)
        parts = rel.split(os.sep)
        if any(p.startswith(".") or p == "__pycache__" for p in parts):
            continue
        for fn in filenames:
            if not fn.endswith(".py") or fn.startswith("_"):
                continue
            full = os.path.join(dirpath, fn)
            norm = os.path.normcase(os.path.normpath(full))
            try:
                mtime = os.path.getmtime(full)
            except Exception:
                mtime = 0.0

            if norm in _loaded_scripts and _loaded_script_mtime.get(norm) == mtime:
                continue
            with open(full, "r", encoding="utf-8", errors="ignore") as f:
                head = f.read(8192)
            if not any(kw in head for kw in keywords):
                continue
            _loaded_scripts.add(norm)
            mod_name = _loaded_script_modules.get(norm)
            if not mod_name:
                mod_name = f"_infengine_disc_{fn[:-3]}_{id(full) & 0xFFFF:04x}"
            spec = importlib.util.spec_from_file_location(mod_name, full)
            if spec and spec.loader:
                mod = importlib.util.module_from_spec(spec)
                parent_dir = os.path.dirname(full)
                added = parent_dir not in sys.path
                if added:
                    sys.path.insert(0, parent_dir)
                spec.loader.exec_module(mod)
                sys.modules[mod_name] = mod
                _loaded_script_modules[norm] = mod_name
                _loaded_script_mtime[norm] = mtime
                if added:
                    sys.path.remove(parent_dir)


def _prune_deleted_loaded_scripts() -> None:
    """Drop cache entries for scripts removed from disk."""
    for norm in list(_loaded_scripts):
        if os.path.exists(norm):
            continue
        _loaded_scripts.discard(norm)
        _loaded_script_mtime.pop(norm, None)
        mod_name = _loaded_script_modules.pop(norm, "")
        if mod_name:
            sys.modules.pop(mod_name, None)


def _collect_subclasses(
    base: type,
    out: Dict[str, type],
    name_attr: str,
) -> None:
    """Recursively collect concrete subclasses into *out*."""
    for cls in base.__subclasses__():
        name = getattr(cls, name_attr, "")
        if name and not name.startswith("_") and _is_live_class(cls):
            out[name] = cls
        # Recurse into deeper subclasses
        _collect_subclasses(cls, out, name_attr)


def _is_live_class(cls: type) -> bool:
    """Return True when a discovered class still points to a live module file.

    This filters stale classes left in ``__subclasses__()`` after users delete
    or move pipeline/pass scripts at runtime.
    """
    mod = sys.modules.get(getattr(cls, "__module__", ""))
    if mod is None:
        return False
    src = getattr(mod, "__file__", "")
    if not src:
        return True
    return os.path.exists(src)
