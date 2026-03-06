import os
from typing import Optional

_project_root: Optional[str] = None


def set_project_root(path: Optional[str]) -> None:
    """Set the current project root for path normalization."""
    global _project_root
    _project_root = os.path.abspath(path) if path else None


def get_project_root() -> Optional[str]:
    """Get the current project root if set."""
    return _project_root


def resolve_script_path(path: Optional[str]) -> Optional[str]:
    """Resolve a possibly relative script path to an absolute path."""
    if not path:
        return path
    if os.path.isabs(path):
        return path
    if _project_root:
        return os.path.abspath(os.path.join(_project_root, path))
    return os.path.abspath(path)
