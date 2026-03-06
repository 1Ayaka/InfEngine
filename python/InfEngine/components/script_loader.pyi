"""Type stubs for InfEngine.components.script_loader."""

from __future__ import annotations

from typing import List, Optional, Type

from .component import InfComponent


class ScriptLoadError(Exception):
    """Raised when a script cannot be loaded or contains no valid components."""
    ...


def load_component_from_file(file_path: str) -> Type[InfComponent]:
    """Load the first InfComponent subclass from a Python file.

    Raises:
        ScriptLoadError: If file doesn't exist, can't be imported,
                         or contains no components.
    """
    ...


def load_all_components_from_file(file_path: str) -> List[Type[InfComponent]]:
    """Load all InfComponent subclasses from a Python file.

    Raises:
        ScriptLoadError: If file doesn't exist or can't be imported.
    """
    ...


def create_component_instance(component_class: Type[InfComponent]) -> InfComponent:
    """Create an instance of a component class.

    Raises:
        ScriptLoadError: If instantiation fails.
    """
    ...


def load_and_create_component(
    file_path: str, asset_database: Optional[object] = ...
) -> InfComponent:
    """Load first component from file and create an instance.

    Raises:
        ScriptLoadError: If loading or instantiation fails.
    """
    ...


def get_component_info(component_class: Type[InfComponent]) -> dict:
    """Extract metadata from a component class.

    Returns:
        Dict with keys ``name``, ``module``, ``docstring``, ``fields``.
    """
    ...
