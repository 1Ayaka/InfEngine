"""
Serialized Field Decorator for InfComponent.

This module provides the @serialized_field decorator that marks class attributes
as serializable and inspector-visible fields.

Usage:
    class MyComponent(InfComponent):
        speed: float = serialized_field(default=5.0, range=(0, 100), tooltip="Movement speed")
        name: str = serialized_field(default="Player")
        target: 'GameObject' = serialized_field(default=None)
"""

from enum import Enum, auto
from typing import Any, Tuple, Optional, Type, Dict, Callable, TYPE_CHECKING
from dataclasses import dataclass
import weakref
import threading

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
    GAME_OBJECT = auto()  # Reference to another GameObject
    COMPONENT = auto()     # Reference to a component
    MATERIAL = auto()      # Reference to a Material asset
    TEXTURE = auto()       # Reference to a Texture asset
    SHADER = auto()        # Reference to a Shader asset
    ASSET = auto()         # Generic asset reference
    ENUM = auto()
    LIST = auto()
    UNKNOWN = auto()


@dataclass
class FieldMetadata:
    """Metadata for a serialized field."""
    name: str
    field_type: FieldType
    default: Any
    range: Optional[Tuple[float, float]] = None  # (min, max) for numeric types
    tooltip: str = ""
    readonly: bool = False
    header: str = ""  # Group header shown above this field
    space: float = 0.0  # Vertical space before this field
    enum_type: Optional[Type[Enum]] = None  # For ENUM fields (or str for lazy resolve)
    enum_labels: Optional[list] = None  # Override display names for ENUM members
    element_type: Optional[FieldType] = None  # For LIST fields
    group: str = ""  # Collapsible group name (fields sharing the same group are folded together)
    info_text: str = ""  # Non-editable description shown below the field (dimmed)
    multiline: bool = False  # STRING: use multiline text input widget
    slider: bool = True  # When range is set, True = slider widget, False = bounded drag
    drag_speed: Optional[float] = None  # Override default drag speed (None = type default)
    required_component: Optional[str] = None  # For GAME_OBJECT: only accept objects with this C++ component
    visible_when: Optional[Callable] = None  # fn(component) → bool; hides field when False

    # For internal use
    python_type: Optional[Type] = None
    getter: Optional[Callable] = None
    setter: Optional[Callable] = None


class SerializedFieldDescriptor:
    """
    Descriptor that handles get/set for serialized fields.
    This enables proper attribute access while maintaining metadata.
    
    Uses weak references to automatically clean up values when instances
    are garbage collected, preventing memory leaks.
    """
    
    def __init__(self, metadata: FieldMetadata):
        self.metadata = metadata
        self._values: Dict[int, Any] = {}  # instance id -> value
        self._weak_refs: Dict[int, weakref.ref] = {}  # instance id -> weak ref
        self._lock = threading.Lock()  # Thread-safe access
    
    def __set_name__(self, owner: Type, name: str):
        self.metadata.name = name
        # Register this field in the owner class
        if '_serialized_fields_' not in owner.__dict__:
            owner._serialized_fields_ = {}
        owner._serialized_fields_[name] = self.metadata
    
    def _cleanup_dead_refs(self):
        """Remove entries for garbage-collected instances."""
        dead_ids = [inst_id for inst_id, ref in self._weak_refs.items() if ref() is None]
        for inst_id in dead_ids:
            self._values.pop(inst_id, None)
            self._weak_refs.pop(inst_id, None)
    
    def __get__(self, instance: Optional['InfComponent'], owner: Type) -> Any:
        if instance is None:
            return self
        inst_id = id(instance)
        with self._lock:
            # Periodic cleanup of dead references
            if len(self._weak_refs) > 100:
                self._cleanup_dead_refs()
            return self._values.get(inst_id, self.metadata.default)
    
    def __set__(self, instance: 'InfComponent', value: Any):
        if self.metadata.readonly:
            raise AttributeError(f"Field '{self.metadata.name}' is readonly")

        inst_id = id(instance)

        # --- Phase 4: Auto-record undo for edit-mode property changes ---
        if not getattr(instance, '_inf_deserializing', False):
            from InfEngine.engine.undo import UndoManager
            mgr = UndoManager.instance()
            if (mgr and not mgr.is_executing and mgr.enabled
                    and hasattr(instance, 'game_object')
                    and instance.game_object is not None):
                from InfEngine.engine.play_mode import PlayModeManager
                pmm = PlayModeManager.get_instance()
                if pmm is None or pmm.is_edit_mode:
                    with self._lock:
                        old_value = self._values.get(inst_id, self.metadata.default)
                    if old_value != value:
                        from InfEngine.engine.undo import SetPropertyCommand
                        mgr.execute(SetPropertyCommand(
                            instance, self.metadata.name,
                            old_value, value, f"Set {self.metadata.name}"))
                        return  # execute() -> setattr -> __set__ with _is_executing=True -> normal path below

        # Normal set path
        with self._lock:
            old = self._values.get(inst_id, self.metadata.default)
            self._values[inst_id] = value
            # Track instance with weak reference for cleanup
            if inst_id not in self._weak_refs:
                self._weak_refs[inst_id] = weakref.ref(instance)

        # Mark scene dirty whenever a serialized field actually changes,
        # even if the undo path above was skipped (play mode, import error, etc.).
        # Skip in play mode — runtime changes are transient.
        if not getattr(instance, '_inf_deserializing', False):
            if old != value:
                from InfEngine.engine.play_mode import PlayModeManager, PlayModeState
                pm = PlayModeManager.get_instance()
                if pm and pm.state != PlayModeState.EDIT:
                    pass  # skip dirty in play mode
                else:
                    from InfEngine.engine.scene_manager import SceneFileManager
                    sfm = SceneFileManager.instance()
                    if sfm is not None:
                        sfm.mark_dirty()
    
    def __delete__(self, instance: 'InfComponent'):
        inst_id = id(instance)
        with self._lock:
            self._values.pop(inst_id, None)
            self._weak_refs.pop(inst_id, None)


def _infer_field_type(python_type: Optional[Type], default: Any) -> FieldType:
    """Infer FieldType from Python type annotation or default value."""
    if python_type is not None:
        type_name = getattr(python_type, '__name__', str(python_type))
        
        if python_type == int:
            return FieldType.INT
        elif python_type == float:
            return FieldType.FLOAT
        elif python_type == bool:
            return FieldType.BOOL
        elif python_type == str:
            return FieldType.STRING
        elif type_name in ('vec2f', 'Vec2', 'Vector2'):
            return FieldType.VEC2
        elif type_name in ('vec3f', 'Vec3', 'Vector3'):
            return FieldType.VEC3
        elif type_name in ('vec4f', 'Vec4', 'Vector4'):
            return FieldType.VEC4
        elif type_name == 'GameObject':
            return FieldType.GAME_OBJECT
        elif type_name == 'Material':
            return FieldType.MATERIAL
        elif type_name == 'TextureRef':
            return FieldType.TEXTURE
        elif type_name == 'ShaderRef':
            return FieldType.SHADER
        elif isinstance(python_type, type) and issubclass(python_type, Enum):
            return FieldType.ENUM
        elif hasattr(python_type, '__origin__') and python_type.__origin__ in (list, tuple):
            return FieldType.LIST
    
    # Infer from default value
    if default is not None:
        if isinstance(default, Enum):
            # Check Enum before int — IntEnum is both int and Enum
            return FieldType.ENUM
        elif isinstance(default, bool):
            return FieldType.BOOL
        elif isinstance(default, int):
            # Python int -> INT (5 is int, 5.0 is float)
            return FieldType.INT
        elif isinstance(default, float):
            return FieldType.FLOAT
        elif isinstance(default, str):
            return FieldType.STRING
        elif isinstance(default, (list, tuple)):
            return FieldType.LIST
        # Check asset ref types by class name (avoids circular import)
        default_type_name = type(default).__name__
        if default_type_name == 'TextureRef':
            return FieldType.TEXTURE
        elif default_type_name == 'ShaderRef':
            return FieldType.SHADER
    
    return FieldType.UNKNOWN


def infer_field_type_from_value(value: Any) -> FieldType:
    """Infer FieldType from a runtime value (for auto-serialized fields)."""
    if value is None:
        return FieldType.UNKNOWN
    return _infer_field_type(type(value), value)


def serialized_field(
    default: Any = None,
    *,
    field_type: Optional[FieldType] = None,
    range: Optional[Tuple[float, float]] = None,
    tooltip: str = "",
    readonly: bool = False,
    header: str = "",
    space: float = 0.0,
    group: str = "",
    info_text: str = "",
    multiline: bool = False,
    slider: bool = True,
    drag_speed: Optional[float] = None,
    required_component: Optional[str] = None,
) -> Any:
    """
    Decorator/descriptor for marking a field as serialized and inspector-visible.
    
    Args:
        default: Default value for the field
        field_type: Explicit field type (auto-detected if not provided)
        range: (min, max) tuple for numeric sliders / bounded drag
        tooltip: Hover text shown in inspector
        readonly: If True, field cannot be modified in inspector
        header: Group header text shown above this field
        space: Vertical spacing before this field in inspector
        group: Collapsible group name.  All consecutive fields with the
            same *group* value are wrapped inside a single
            ``collapsing_header`` section.
        info_text: Non-editable description line rendered after the field
            widget in dimmed text.  Useful for hints and explanations.
        multiline: If True and the field is STRING, render a multiline
            text input widget instead of a single-line one.
        slider: When ``range`` is set, controls the widget style.
            ``True`` (default) = slider, ``False`` = bounded drag.
        drag_speed: Override the default drag speed for numeric fields.
            ``None`` means use the type default (0.1 for float, 1.0 for int).
        required_component: For GAME_OBJECT fields only.  If set, only
            GameObjects that have a C++ component with this type name
            (e.g. ``"MeshRenderer"``) will be accepted when dragged from
            the Hierarchy panel.
    
    Returns:
        A descriptor that manages the field value and metadata
    
    Example:
        class MyComponent(InfComponent):
            speed: float = serialized_field(default=5.0, range=(0, 100))
            name: str = serialized_field(default="Player", tooltip="Object name")
            debug: bool = serialized_field(default=False, header="Debug Options")
            text: str = serialized_field(default="Hi", group="Content")
    """
    # Infer field type if not provided
    inferred_type = field_type or _infer_field_type(None, default)
    
    # Auto-detect enum_type from default
    enum_type = None
    if isinstance(default, Enum):
        enum_type = type(default)
    
    metadata = FieldMetadata(
        name="",  # Will be set by __set_name__
        field_type=inferred_type,
        default=default,
        range=range,
        tooltip=tooltip,
        readonly=readonly,
        header=header,
        space=space,
        enum_type=enum_type,
        group=group,
        info_text=info_text,
        multiline=multiline,
        slider=slider,
        drag_speed=drag_speed,
        required_component=required_component,
    )
    
    return SerializedFieldDescriptor(metadata)


def get_serialized_fields(component_class: Type['InfComponent']) -> Dict[str, FieldMetadata]:
    """
    Get all serialized fields from a component class.
    
    Args:
        component_class: The InfComponent subclass to inspect
        
    Returns:
        Dictionary mapping field names to their metadata
    """
    fields = {}
    
    # Walk up the MRO to collect inherited fields
    for cls in reversed(component_class.__mro__):
        if hasattr(cls, '_serialized_fields_'):
            fields.update(cls._serialized_fields_)
    
    return fields


def get_field_value(component: 'InfComponent', field_name: str) -> Any:
    """Get the value of a serialized field."""
    return getattr(component, field_name)


def set_field_value(component: 'InfComponent', field_name: str, value: Any):
    """Set the value of a serialized field."""
    setattr(component, field_name, value)


class HiddenField:
    """
    Marker class for fields that should not be serialized or shown in Inspector.
    
    Use hide_field() to create instances of this class.
    """
    def __init__(self, default: Any = None):
        self.default = default
    
    def __set_name__(self, owner, name):
        self._name = name
    
    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        return getattr(obj, f'_hidden_{self._name}', self.default)
    
    def __set__(self, obj, value):
        setattr(obj, f'_hidden_{self._name}', value)


def hide_field(default: Any = None) -> Any:
    """
    Mark a class-level field as hidden (not serialized, not shown in Inspector).
    
    Use this for internal state that shouldn't be exposed to the editor.
    
    Args:
        default: Default value for the field
    
    Example:
        class MyComponent(InfComponent):
            speed = 5.0           # Serialized, shown in Inspector
            _internal = 0         # Not serialized (private, starts with _)
            cache = hide_field()  # Not serialized, but public API
    """
    return HiddenField(default)


def int_field(
    default: int = 0,
    *,
    range: Optional[Tuple[float, float]] = None,
    tooltip: str = "",
    readonly: bool = False,
    header: str = "",
    space: float = 0.0,
    group: str = "",
    info_text: str = "",
    slider: bool = True,
    drag_speed: Optional[float] = None,
) -> Any:
    """
    Shortcut for creating an integer serialized field.
    
    Equivalent to: serialized_field(default=..., field_type=FieldType.INT, ...)
    
    Args:
        default: Default integer value
        range: (min, max) tuple for slider / bounded drag
        tooltip: Hover text in inspector
        readonly: If True, field cannot be modified
        header: Group header text
        space: Vertical spacing before field
        group: Collapsible group name
        info_text: Non-editable description line (dimmed)
        slider: Widget style when range is set (True = slider, False = drag)
        drag_speed: Override default drag speed
    
    Example:
        class MyComponent(InfComponent):
            count = int_field(default=5, range=(0, 100))
    """
    return serialized_field(
        default=default,
        field_type=FieldType.INT,
        range=range,
        tooltip=tooltip,
        readonly=readonly,
        header=header,
        space=space,
        group=group,
        info_text=info_text,
        slider=slider,
        drag_speed=drag_speed,
    )
