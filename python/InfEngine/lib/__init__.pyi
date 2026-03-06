"""Type stubs for InfEngine.lib."""

from __future__ import annotations

lib_dir: str

# Re-exports everything from _InfEngine (the compiled pybind11 module)
from ._InfEngine import *
