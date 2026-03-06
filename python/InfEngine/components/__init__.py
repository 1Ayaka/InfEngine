"""
InfEngine Component System

Provides Python-based component definition for the Entity-Component system.
Users can create custom components by inheriting from InfComponent.

Example:
    from InfEngine.components import InfComponent, serialized_field
    
    class PlayerController(InfComponent):
        speed: float = serialized_field(default=5.0, range=(0, 100), tooltip="Movement speed")
        
        def start(self):
            print(f"Player started with speed {self.speed}")
        
        def update(self, delta_time: float):
            pos = self.transform.position
            # Move logic...
"""

from .component import InfComponent
from .builtin_component import BuiltinComponent, CppProperty
from .builtin import Light, MeshRenderer, Camera
from .builtin import AudioSource, AudioListener
from .serialized_field import serialized_field, int_field, hide_field, FieldType, get_serialized_fields, get_field_value, set_field_value
from .ref_wrappers import GameObjectRef, MaterialRef
from .inspector import (
    ComponentInspector,
    InspectorData,
)
from .script_loader import (
    load_component_from_file,
    load_all_components_from_file,
    create_component_instance,
    load_and_create_component,
    get_component_info,
    ScriptLoadError,
)
from .registry import (
    get_type,
    get_all_types,
    T,
)
from .decorators import (
    require_component,
    disallow_multiple,
    execute_in_edit_mode,
    add_component_menu,
    icon,
    help_url,
    # Unity-style aliases
    RequireComponent,
    DisallowMultipleComponent,
    ExecuteInEditMode,
    AddComponentMenu,
    HelpURL,
    Icon,
)

__all__ = [
    "InfComponent",
    "BuiltinComponent",
    "CppProperty",
    "Light",
    "MeshRenderer",
    "Camera",
    "AudioSource",
    "AudioListener",
    "serialized_field",
    "int_field",
    "hide_field",
    "FieldType",
    "GameObjectRef",
    "MaterialRef",
    "get_serialized_fields",
    "get_field_value",
    "set_field_value",
    "ComponentInspector",
    "InspectorData",
    "load_component_from_file",
    "load_all_components_from_file",
    "create_component_instance",
    "load_and_create_component",
    "get_component_info",
    "ScriptLoadError",
    # Type lookup
    "get_type",
    "get_all_types",
    "T",
    # Decorators
    "require_component",
    "disallow_multiple",
    "execute_in_edit_mode",
    "add_component_menu",
    "icon",
    "help_url",
    "RequireComponent",
    "DisallowMultipleComponent",
    "ExecuteInEditMode",
    "AddComponentMenu",
    "HelpURL",
    "Icon",
]
