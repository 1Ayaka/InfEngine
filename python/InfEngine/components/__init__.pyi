"""Type stubs for InfEngine.components package."""

from InfEngine.components.component import InfComponent as InfComponent
from InfEngine.components.decorators import (
    AddComponentMenu as AddComponentMenu,
    DisallowMultipleComponent as DisallowMultipleComponent,
    ExecuteInEditMode as ExecuteInEditMode,
    HelpURL as HelpURL,
    Icon as Icon,
    RequireComponent as RequireComponent,
    add_component_menu as add_component_menu,
    disallow_multiple as disallow_multiple,
    execute_in_edit_mode as execute_in_edit_mode,
    help_url as help_url,
    icon as icon,
    require_component as require_component,
)
from InfEngine.components.inspector import (
    ComponentInspector as ComponentInspector,
    InspectorData as InspectorData,
    get_inspector_json as get_inspector_json,
    get_scene_hierarchy_json as get_scene_hierarchy_json,
)
from InfEngine.components.registry import (
    T as T,
    get_all_types as get_all_types,
    get_type as get_type,
)
from InfEngine.components.script_loader import (
    ScriptLoadError as ScriptLoadError,
    create_component_instance as create_component_instance,
    get_component_info as get_component_info,
    load_all_components_from_file as load_all_components_from_file,
    load_and_create_component as load_and_create_component,
    load_component_from_file as load_component_from_file,
)
from InfEngine.components.serialized_field import (
    FieldMetadata as FieldMetadata,
    FieldType as FieldType,
    get_field_value as get_field_value,
    get_serialized_fields as get_serialized_fields,
    hide_field as hide_field,
    int_field as int_field,
    serialized_field as serialized_field,
    set_field_value as set_field_value,
)

__all__ = [
    "InfComponent",
    "serialized_field",
    "int_field",
    "hide_field",
    "FieldType",
    "FieldMetadata",
    "get_serialized_fields",
    "get_field_value",
    "set_field_value",
    "ComponentInspector",
    "InspectorData",
    "get_inspector_json",
    "get_scene_hierarchy_json",
    "load_component_from_file",
    "load_all_components_from_file",
    "create_component_instance",
    "load_and_create_component",
    "get_component_info",
    "ScriptLoadError",
    "get_type",
    "get_all_types",
    "T",
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
