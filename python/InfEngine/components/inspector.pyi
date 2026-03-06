"""Type stubs for InfEngine.components.inspector."""

from __future__ import annotations

from typing import Any, Dict, List, TYPE_CHECKING

if TYPE_CHECKING:
    from InfEngine.lib import Component, GameObject, Scene


class InspectorData:
    """Container for inspector-displayable component data."""
    component_type: str
    fields: Dict[str, Any]
    enabled: bool
    def __init__(self, component_type: str) -> None: ...
    def add_field(self, name: str, value: Any, field_type: str = ...) -> None: ...
    def to_dict(self) -> Dict[str, Any]: ...


class ComponentInspector:
    """Utility class for inspecting GameObject components."""
    @staticmethod
    def get_cpp_component_data(component: Component) -> InspectorData: ...
    @staticmethod
    def get_python_component_data(py_component: Any) -> InspectorData: ...
    @staticmethod
    def get_gameobject_inspector_data(game_object: GameObject) -> Dict[str, Any]: ...
    @staticmethod
    def get_scene_hierarchy_data(scene: Scene) -> List[Dict[str, Any]]: ...


def get_inspector_json(game_object: GameObject) -> str:
    """Get complete inspector data as a JSON string."""
    ...


def get_scene_hierarchy_json(scene: Scene) -> str:
    """Get scene hierarchy as a JSON string."""
    ...
