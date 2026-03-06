"""Type stubs for InfEngine.components.registry.

Uses ``InfComponent.__subclasses__()`` recursively — no manual registration needed.
"""

from __future__ import annotations

from typing import Dict, Optional, Type, TYPE_CHECKING

if TYPE_CHECKING:
    from .component import InfComponent


def get_type(name: str) -> Optional[Type[InfComponent]]:
    """Get a component class by its name (recursive ``__subclasses__()`` scan).

    Example::

        TestCache = get_type("testcache")
        if TestCache:
            comp = self.get_component(TestCache)
    """
    ...


def get_all_types() -> Dict[str, Type[InfComponent]]:
    """Get all known InfComponent subclass types as ``{name: class}``."""
    ...


class _TypeAccessor:
    """Dynamic attribute accessor for component types.

    Example::

        T.TestCache   # -> get_type("TestCache")
        T.Movement    # -> get_type("Movement")
    """
    def __getattr__(self, name: str) -> Optional[Type[InfComponent]]: ...
    def __repr__(self) -> str: ...


T: _TypeAccessor
