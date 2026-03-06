"""
Component Type Lookup for InfEngine.

Uses Python's built-in ``__subclasses__()`` to discover all InfComponent
subclasses at runtime — no manual registration needed.

Usage:
    from InfEngine.components import get_type, T

    # Get component type by name
    TestCache = get_type("testcache")
    comp = self.get_component(TestCache)

    # Or use the T shorthand
    comp = self.get_component(T.testcache)
"""

from typing import Dict, Type, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from .component import InfComponent


def _find_subclass(base: type, name: str) -> Optional[Type['InfComponent']]:
    """Recursively search *base*.__subclasses__() for a class whose __name__ == *name*."""
    for cls in base.__subclasses__():
        if cls.__name__ == name:
            return cls
        found = _find_subclass(cls, name)
        if found is not None:
            return found
    return None


def get_type(name: str) -> Optional[Type['InfComponent']]:
    """
    Get a component class by its name.

    Walks the full InfComponent subclass tree via ``__subclasses__()``.

    Args:
        name: The class name (e.g., "testcache", "PlayerController")

    Returns:
        The component class, or None if not found
    """
    from .component import InfComponent
    return _find_subclass(InfComponent, name)


def get_all_types() -> Dict[str, Type['InfComponent']]:
    """
    Get all known InfComponent subclass types.

    Returns:
        Dictionary of class_name -> class_type
    """
    from .component import InfComponent
    result: Dict[str, Type['InfComponent']] = {}

    def _collect(base: type) -> None:
        for cls in base.__subclasses__():
            result[cls.__name__] = cls
            _collect(cls)

    _collect(InfComponent)
    return result


class _TypeAccessor:
    """
    Dynamic attribute accessor for component types.

    Allows accessing component types as attributes:
        T.testcache  -> get_type("testcache")
        T.Movement   -> get_type("Movement")
    """

    def __getattr__(self, name: str) -> Optional[Type['InfComponent']]:
        """Get component type by attribute access."""
        result = get_type(name)
        if result is None:
            return None
        return result

    def __repr__(self) -> str:
        types = list(get_all_types().keys())
        return f"<ComponentTypes: {types}>"


# Global instance for easy access: T.MyComponent
T = _TypeAccessor()
