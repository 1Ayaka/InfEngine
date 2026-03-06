"""Type stubs for InfEngine.components.serialized_field."""

from __future__ import annotations

from enum import Enum, auto
from typing import Any, Callable, Dict, Optional, Tuple, Type, TYPE_CHECKING
from dataclasses import dataclass

if TYPE_CHECKING:
    from .component import InfComponent


class FieldType(Enum):
    """Supported field types for serialization and inspector rendering."""
    INT = auto()
    FLOAT = auto()
    BOOL = auto()
    STRING = auto()
    VEC2 = auto()
    VEC3 = auto()
    VEC4 = auto()
    COLOR = auto()
    GAME_OBJECT = auto()
    COMPONENT = auto()
    MATERIAL = auto()
    TEXTURE = auto()
    SHADER = auto()
    ASSET = auto()
    ENUM = auto()
    LIST = auto()
    UNKNOWN = auto()


@dataclass
class FieldMetadata:
    """Metadata for a serialized field."""
    name: str
    field_type: FieldType
    default: Any
    range: Optional[Tuple[float, float]] = ...
    tooltip: str = ...
    readonly: bool = ...
    header: str = ...
    space: float = ...
    enum_type: Optional[Type[Enum]] = ...
    enum_labels: Optional[list] = ...
    element_type: Optional[FieldType] = ...
    group: str = ...
    info_text: str = ...
    multiline: bool = ...
    slider: bool = ...
    drag_speed: Optional[float] = ...
    required_component: Optional[str] = ...
    visible_when: Optional[Callable] = ...
    python_type: Optional[Type] = ...
    getter: Optional[Callable] = ...
    setter: Optional[Callable] = ...


class SerializedFieldDescriptor:
    """Descriptor that handles get/set for serialized fields."""
    metadata: FieldMetadata
    def __init__(self, metadata: FieldMetadata) -> None: ...
    def __set_name__(self, owner: Type, name: str) -> None: ...
    def __get__(self, instance: Optional[InfComponent], owner: Type) -> Any: ...
    def __set__(self, instance: InfComponent, value: Any) -> None: ...
    def __delete__(self, instance: InfComponent) -> None: ...


class HiddenField:
    """Marker class for fields hidden from serialization and Inspector."""
    default: Any
    def __init__(self, default: Any = ...) -> None: ...
    def __set_name__(self, owner: type, name: str) -> None: ...
    def __get__(self, obj: Any, objtype: Any = ...) -> Any: ...
    def __set__(self, obj: Any, value: Any) -> None: ...


def infer_field_type_from_value(value: Any) -> FieldType:
    """Infer FieldType from a runtime value."""
    ...


def serialized_field(
    default: Any = ...,
    *,
    field_type: Optional[FieldType] = ...,
    range: Optional[Tuple[float, float]] = ...,
    tooltip: str = ...,
    readonly: bool = ...,
    header: str = ...,
    space: float = ...,
    group: str = ...,
    info_text: str = ...,
    multiline: bool = ...,
    slider: bool = ...,
    drag_speed: Optional[float] = ...,
    required_component: Optional[str] = ...,
) -> Any:
    """Mark a field as serialized and inspector-visible.

    Args:
        default: Default value for the field.
        field_type: Explicit field type (auto-detected if not provided).
        range: ``(min, max)`` tuple for numeric sliders / bounded drag.
        tooltip: Hover text shown in inspector.
        readonly: If ``True``, field is read-only in inspector.
        header: Group header text shown above this field.
        space: Vertical spacing before this field in inspector.
        group: Collapsible group name.
        info_text: Non-editable description line (dimmed) below the field.
        multiline: Use multiline text input for STRING fields.
        slider: Widget style when range is set (True = slider, False = drag).
        drag_speed: Override default drag speed for numeric fields.
        required_component: For GAME_OBJECT fields only. If set, only
            GameObjects with a C++ component of this type name are accepted.

    Example::

        class MyComponent(InfComponent):
            speed: float = serialized_field(default=5.0, range=(0, 100))
    """
    ...


def hide_field(default: Any = ...) -> Any:
    """Mark a class-level field as hidden (not serialized, not in Inspector)."""
    ...


def int_field(
    default: int = ...,
    *,
    range: Optional[Tuple[float, float]] = ...,
    tooltip: str = ...,
    readonly: bool = ...,
    header: str = ...,
    space: float = ...,
    group: str = ...,
    info_text: str = ...,
    slider: bool = ...,
    drag_speed: Optional[float] = ...,
) -> Any:
    """Shortcut for creating an integer serialized field."""
    ...


def get_serialized_fields(component_class: Type[InfComponent]) -> Dict[str, FieldMetadata]:
    """Get all serialized fields from a component class (including inherited)."""
    ...


def get_field_value(component: InfComponent, field_name: str) -> Any:
    """Get the value of a serialized field."""
    ...


def set_field_value(component: InfComponent, field_name: str, value: Any) -> None:
    """Set the value of a serialized field."""
    ...
