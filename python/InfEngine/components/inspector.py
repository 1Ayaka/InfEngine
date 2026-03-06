"""
Inspector utility for reading and displaying serialized component data.

This module provides helpers to extract serialized field information
from both C++ and Python components for use in Inspector UI.
"""

import json
from typing import Dict, Any, List, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from InfEngine.lib import GameObject, Component

from InfEngine.components import get_serialized_fields, get_field_value


class InspectorData:
    """Container for inspector-displayable component data."""
    
    def __init__(self, component_type: str):
        self.component_type = component_type
        self.fields: Dict[str, Any] = {}
        self.enabled: bool = True
    
    def add_field(self, name: str, value: Any, field_type: str = "unknown"):
        """Add a field to the inspector data."""
        self.fields[name] = {
            "value": value,
            "type": field_type
        }
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return {
            "type": self.component_type,
            "enabled": self.enabled,
            "fields": self.fields
        }


class ComponentInspector:
    """Utility class for inspecting GameObject components."""
    
    @staticmethod
    def get_cpp_component_data(component: 'Component') -> InspectorData:
        """
        Extract serialized data from a raw C++ component (Transform only).
        
        Light, MeshRenderer, Camera now go through BuiltinComponent wrappers
        and use ``get_python_component_data()`` instead.
        
        Args:
            component: C++ Component instance
            
        Returns:
            InspectorData with extracted fields
        """
        # Get serialized JSON from C++ component
        json_str = component.serialize()
        data = json.loads(json_str)
        
        inspector_data = InspectorData(component.type_name)
        inspector_data.enabled = data.get("enabled", True)
        
        # Transform — special handling for well-known fields
        if component.type_name == "Transform":
            if "position" in data:
                inspector_data.add_field("position", data["position"], "vec3")
            if "rotation" in data:
                inspector_data.add_field("rotation", data["rotation"], "vec3")
            if "scale" in data:
                inspector_data.add_field("scale", data["scale"], "vec3")
            return inspector_data

        # Generic fallback for any unknown C++ type
        handled_keys = {"schema_version", "type", "enabled", "component_id"}

        for key, value in data.items():
            if key in handled_keys:
                continue

            if isinstance(value, bool):
                inspector_data.add_field(key, value, "bool")
            elif isinstance(value, int):
                inspector_data.add_field(key, value, "int")
            elif isinstance(value, float):
                inspector_data.add_field(key, value, "float")
            elif isinstance(value, str):
                inspector_data.add_field(key, value, "string")
            elif isinstance(value, list):
                if len(value) == 2 and all(isinstance(v, (int, float)) for v in value):
                    inspector_data.add_field(key, value, "vec2")
                elif len(value) == 3 and all(isinstance(v, (int, float)) for v in value):
                    inspector_data.add_field(key, value, "vec3")
                elif len(value) == 4 and all(isinstance(v, (int, float)) for v in value):
                    inspector_data.add_field(key, value, "vec4")
                else:
                    inspector_data.add_field(key, value, "array")
            elif isinstance(value, dict):
                inspector_data.add_field(key, value, "json")
            else:
                inspector_data.add_field(key, str(value), "string")
        
        return inspector_data
    
    @staticmethod
    def get_python_component_data(py_component) -> InspectorData:
        """
        Extract serialized field data from a Python InfComponent.
        
        Args:
            py_component: Python InfComponent instance
            
        Returns:
            InspectorData with serialized fields
        """
        inspector_data = InspectorData(py_component.type_name)
        inspector_data.enabled = py_component.enabled
        
        # Get serialized fields from the component class
        fields = get_serialized_fields(type(py_component))
        
        for field_name, metadata in fields.items():
            value = get_field_value(py_component, field_name)
            field_type = metadata.field_type.name.lower()
            
            inspector_data.add_field(field_name, value, field_type)
        
        return inspector_data
    
    @staticmethod
    def get_gameobject_inspector_data(game_object: 'GameObject') -> Dict[str, Any]:
        """
        Get complete inspector data for a GameObject including all components.
        
        Args:
            game_object: GameObject to inspect
            
        Returns:
            Dictionary with GameObject info and all component data
        """
        result = {
            "name": game_object.name,
            "id": game_object.id,
            "active": game_object.active,
            "components": []
        }
        
        # Get Transform (always present)
        transform = game_object.get_transform()
        transform_data = ComponentInspector.get_cpp_component_data(transform)
        result["components"].append(transform_data.to_dict())
        
        # Get all other components (unified iteration to avoid duplicates)
        for component in game_object.get_components():
            # Skip Transform (already added above)
            if hasattr(component, 'type_name') and component.type_name == "Transform":
                continue
            
            # Check if it's a PyComponentProxy (wraps Python component)
            if hasattr(component, 'get_py_component'):
                py_comp = component.get_py_component()
                if py_comp is not None:
                    comp_data = ComponentInspector.get_python_component_data(py_comp)
                else:
                    continue
            else:
                # C++ component — try to use a BuiltinComponent wrapper first.
                # This provides serialized-field metadata for the Inspector
                # instead of raw JSON parsing.
                comp_data = None
                from .builtin_component import BuiltinComponent
                type_name = getattr(component, 'type_name', '')
                wrapper_cls = BuiltinComponent._builtin_registry.get(type_name)
                if wrapper_cls is not None:
                    wrapper = wrapper_cls._get_or_create_wrapper(
                        component, game_object
                    )
                    comp_data = ComponentInspector.get_python_component_data(wrapper)

                # Fall back to C++ JSON parsing for unregistered types
                if comp_data is None:
                    comp_data = ComponentInspector.get_cpp_component_data(component)
            
            result["components"].append(comp_data.to_dict())
        
        # NOTE: Do NOT iterate get_py_components() separately!
        # Python components are already handled via PyComponentProxy above.
        # Doing both would cause duplicate entries.
        
        return result
    
    @staticmethod
    def get_scene_hierarchy_data(scene: 'Scene') -> List[Dict[str, Any]]:
        """
        Get hierarchical scene data for display in a tree view.
        
        Args:
            scene: Scene to inspect
            
        Returns:
            List of root GameObject data with nested children
        """
        def serialize_object_hierarchy(obj: 'GameObject') -> Dict[str, Any]:
            obj_data = {
                "name": obj.name,
                "id": obj.id,
                "active": obj.active,
                "children": []
            }
            
            # Recursively add children
            for child in obj.get_children():
                obj_data["children"].append(serialize_object_hierarchy(child))
            
            return obj_data
        
        hierarchy = []
        for root_obj in scene.get_root_objects():
            hierarchy.append(serialize_object_hierarchy(root_obj))
        
        return hierarchy
