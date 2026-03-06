"""
InfComponent - Base class for all Python-defined components.

Provides Unity-style lifecycle methods and property injection.
Users inherit from this class to create custom game logic.

Example:
    from InfEngine.components import InfComponent, serialized_field
    
    class PlayerController(InfComponent):
        speed: float = serialized_field(default=5.0)
        
        def start(self):
            print("Player started!")
        
        def update(self, delta_time: float):
            pos = self.transform.position
            self.transform.position = vec3f(pos.x + self.speed * delta_time, pos.y, pos.z)
"""

from typing import Optional, Dict, Any, Type, TYPE_CHECKING, List
import threading
import weakref

if TYPE_CHECKING:
    from InfEngine.lib import GameObject, Transform
    from InfEngine.coroutine import Coroutine


class InfComponent:
    """
    Base class for Python-defined components.
    
    Lifecycle methods (override as needed):
        - awake(): Called when component is first added
        - start(): Called before first update, after all awake()
        - update(delta_time): Called every frame
        - late_update(delta_time): Called after all update()
        - on_destroy(): Called when component is removed/destroyed
        - on_enable(): Called when component becomes enabled
        - on_disable(): Called when component becomes disabled
    
    Serialized fields:
        - Use class-level ``serialized_field()`` declarations.

    Injected properties (read-only):
        - game_object: The GameObject this component is attached to
        - transform: Shortcut to game_object.get_transform()
        - enabled: Whether this component is active
        - component_id: Stable unique ID for this component instance
    """
    
    # Class-level storage for serialized field metadata
    _serialized_fields_: Dict[str, Any] = {}

    # Schema version for serialization compatibility
    __schema_version__ = 1

    # Active instance registry: go_id (int) → list of live InfComponent instances.
    # Used by GizmosCollector to skip the expensive get_all_objects() + get_py_components()
    # scene walk — instead we iterate only objects that actually have Python components.
    # Populated by _set_game_object(), removed by _call_on_destroy() or
    # _invalidate_native_binding() during scene rebuild/destruction.
    # Use a plain dict (not WeakValueDictionary) so we hold strong refs; instances
    # remove themselves from the registry in _call_on_destroy().
    _active_instances: Dict[int, List['InfComponent']] = {}

    # Component category for the Add Component menu.
    # Override in subclasses to group related components together.
    # Examples: "Physics", "Rendering", "Audio", "UI", etc.
    # When empty, script components default to the "Scripts" group.
    _component_category_: str = ""

    # Gizmo visibility: when True, on_draw_gizmos() is called every frame
    # for this component.  When False, on_draw_gizmos() is only called when
    # the owning GameObject (or one of its ancestors) is selected.
    # Subclasses can override: ``always_show = False``
    always_show: bool = True
    
    # Thread-safe component ID generator
    _next_component_id: int = 1
    _id_lock: threading.Lock = threading.Lock()
    
    def __init_subclass__(cls, **kwargs):
        """
        Called when a subclass is created. Collect class-level fields as serialized fields.
        
        This enables Unity-style field declaration:
            class Kobe(InfComponent):
                speed = 5.0
                count = int_field(10)
        """
        super().__init_subclass__(**kwargs)

        # ---- Enforce lifecycle: forbid __init__ override ----
        if '__init__' in cls.__dict__:
            raise TypeError(
                f"{cls.__qualname__} overrides __init__, which is forbidden. "
                f"InfComponent manages its own initialization internally. "
                f"Use awake() for one-time setup or start() for deferred init."
            )
        
        # Always create a fresh dict for this class (don't inherit from parent)
        cls._serialized_fields_ = {}
        
        # Only scan attributes defined directly on this class (not inherited)
        for attr_name in cls.__dict__:
            if attr_name.startswith('_'):
                continue
            
            # Get the raw attribute from class __dict__ to avoid descriptor protocol
            attr = cls.__dict__[attr_name]
            
            # Skip methods, properties, classmethods, staticmethods
            if callable(attr):
                continue
            if isinstance(attr, (property, classmethod, staticmethod)):
                continue
            
            # Skip None values
            if attr is None:
                continue
            
            # Register this field if it's a simple value or FieldMetadata
            from .serialized_field import (
                FieldMetadata, HiddenField, SerializedFieldDescriptor,
                infer_field_type_from_value,
            )
            
            # Skip hidden fields (marked with hide_field())
            if isinstance(attr, HiddenField):
                continue

            # CppProperty — delegates to a C++ component attribute.
            # Recognised via duck-typing marker to avoid circular imports
            # with builtin_component.py.
            if getattr(attr, '_is_cpp_property', False):
                if hasattr(attr, 'metadata'):
                    attr.metadata.name = attr_name
                    cls._serialized_fields_[attr_name] = attr.metadata
                continue
            
            # SerializedFieldDescriptor — __set_name__ already stored its
            # metadata, but we wiped _serialized_fields_ above.  Restore it.
            if isinstance(attr, SerializedFieldDescriptor):
                cls._serialized_fields_[attr_name] = attr.metadata
            elif isinstance(attr, FieldMetadata):
                # Already a field metadata object
                cls._serialized_fields_[attr_name] = attr
            else:
                # Create metadata AND a descriptor from the plain value.
                # This ensures __set__ is intercepted for dirty-tracking
                # and undo recording, just like serialized_field() fields.
                from enum import Enum as _Enum
                field_type = infer_field_type_from_value(attr)
                enum_type = type(attr) if isinstance(attr, _Enum) else None
                metadata = FieldMetadata(
                    name=attr_name,
                    field_type=field_type,
                    default=attr,
                    enum_type=enum_type,
                )
                descriptor = SerializedFieldDescriptor(metadata)
                descriptor.__set_name__(cls, attr_name)
                setattr(cls, attr_name, descriptor)
                cls._serialized_fields_[attr_name] = metadata
    
    def __init__(self):
        """Internal framework initialization — **do not override**.

        Subclasses must use lifecycle methods instead:
        - ``awake()`` — called once when the component is first created
        - ``start()`` — called before the first ``update()``, after all ``awake()``
        - ``on_destroy()`` — called when the component is removed / scene unloaded

        Overriding ``__init__`` is enforced as a ``TypeError`` at class
        creation time (see ``__init_subclass__``).
        """
        # Generate stable component ID immediately (thread-safe)
        with InfComponent._id_lock:
            self._component_id = InfComponent._next_component_id
            InfComponent._next_component_id += 1
        
        self._game_object: Optional['GameObject'] = None  # Reference to the owning GameObject
        self._game_object_ref: Optional[weakref.ref] = None  # Weak reference for safety
        self._cpp_component = None  # Native lifecycle authority (PyComponentProxy or built-in C++ component)
        self._enabled = True
        self._execution_order = 0
        self._has_started = False
        self._awake_called = False
        self._is_destroyed = False  # Track destruction state
        self._component_name = self.__class__.__name__
        self._script_guid: Optional[str] = None
        self._registered_go_id: Optional[int] = None  # go_id this comp is registered under
        self._native_generation: int = 0
        
        # Component lookup cache (invalidated when components change)
        self._component_cache: Dict[Type, Any] = {}
        self._cache_valid = False

        # Coroutine scheduler (lazy-created on first start_coroutine call)
        self._coroutine_scheduler = None
        
        # Initialize serialized fields with defaults (from class-level declarations)
        self._init_serialized_fields()

    def _init_serialized_fields(self):
        """Initialize all serialized fields with their default values."""
        from .serialized_field import get_serialized_fields
        fields = get_serialized_fields(self.__class__)
        for name, metadata in fields.items():
            # The descriptor handles this automatically, but we ensure instance exists
            if not hasattr(self, name) or getattr(self, name) is None:
                if metadata.default is not None:
                    setattr(self, name, metadata.default)

    # ========================================================================
    # Property Injection (set by the engine)
    # ========================================================================
    
    @property
    def game_object(self) -> Optional['GameObject']:
        """Get the GameObject this component is attached to."""
        if self._is_destroyed:
            return None
        cpp_component = self._get_bound_native_component()
        if cpp_component is not None:
            try:
                go = cpp_component.game_object
            except Exception:
                self._invalidate_native_binding()
                return None
            if go is None:
                self._invalidate_native_binding()
                return None
            self._game_object = go
            self._game_object_ref = weakref.ref(go)
            return go
        # Use weak reference if available for safety
        if self._game_object_ref is not None:
            go = self._game_object_ref()
            if go is None:
                # GameObject was destroyed, mark component as invalid
                self._game_object = None
                return None
            return go
        return self._game_object
    
    @property
    def transform(self) -> Optional['Transform']:
        """Get the Transform of the attached GameObject (safe access)."""
        if self._is_destroyed:
            return None
        go = self.game_object
        if go is not None:
            try:
                return go.get_transform()
            except Exception:
                self._invalidate_native_binding()
                return None
        return None
    
    @property
    def is_valid(self) -> bool:
        """Check if this component is still valid (not destroyed, has game_object)."""
        return not self._is_destroyed and self.game_object is not None
    
    @property
    def enabled(self) -> bool:
        """Check if this component is enabled."""
        cpp_component = self._get_bound_native_component()
        if cpp_component is not None:
            try:
                self._enabled = bool(cpp_component.enabled)
            except Exception:
                self._invalidate_native_binding()
        return self._enabled
    
    @enabled.setter
    def enabled(self, value: bool):
        """Enable or disable this component.

        Native lifecycle is always authoritative once the component is bound.
        Before binding, the value is simply staged on the Python instance and
        will be consumed by the native proxy when attached.
        """
        if self._is_destroyed:
            return
        value = bool(value)
        if self._enabled == value and getattr(self, '_cpp_component', None) is None:
            return

        cpp_component = getattr(self, '_cpp_component', None)
        if cpp_component is not None:
            cpp_component.enabled = value
            return

        self._enabled = value
    
    @property
    def type_name(self) -> str:
        """Get the component type name."""
        return self._component_name

    @property
    def execution_order(self) -> int:
        """Execution order (lower value runs earlier)."""
        cpp_component = self._get_bound_native_component()
        if cpp_component is not None:
            try:
                return int(cpp_component.execution_order)
            except Exception:
                self._invalidate_native_binding()
                return int(getattr(self, '_execution_order', 0))
        return int(getattr(self, '_execution_order', 0))

    @execution_order.setter
    def execution_order(self, value: int):
        cpp_component = getattr(self, '_cpp_component', None)
        if cpp_component is not None:
            cpp_component.execution_order = int(value)
            return
        self._execution_order = int(value)

    @property
    def component_id(self) -> int:
        """Get the stable component ID (assigned at construction)."""
        return self._component_id
    
    # ========================================================================
    # Internal methods (called by the engine)
    # ========================================================================
    
    def _set_game_object(self, game_object: 'GameObject'):
        """Internal: Set the owning GameObject. Called by the engine."""
        # ---- Update active-instances registry ----
        self._remove_from_active_registry()

        self._game_object = game_object
        # Also store weak reference for safe access
        if game_object is not None:
            self._game_object_ref = weakref.ref(game_object)
            # Register into active-instances table (works in both edit & play modes)
            go_id = game_object.id
            lst = InfComponent._active_instances.get(go_id)
            if lst is None:
                InfComponent._active_instances[go_id] = [self]
            else:
                if self not in lst:   # guard against duplicate calls
                    lst.append(self)
            self._registered_go_id = go_id
        else:
            self._game_object_ref = None
        # Invalidate component cache
        self._invalidate_cache()

    def _remove_from_active_registry(self):
        """Remove this component from the active-instance registry."""
        old_id = getattr(self, '_registered_go_id', None)
        if old_id is None:
            return

        lst = InfComponent._active_instances.get(old_id)
        if lst is not None:
            if self in lst:
                lst.remove(self)
            if not lst:
                InfComponent._active_instances.pop(old_id, None)
        self._registered_go_id = None

    def _bind_native_component(self, cpp_component, game_object=None):
        """Bind this Python instance to its native lifecycle authority."""
        self._cpp_component = cpp_component
        self._native_generation += 1
        if game_object is not None:
            self._set_game_object(game_object)

        if cpp_component is not None:
            try:
                self._component_id = int(cpp_component.component_id)
            except Exception:
                pass
            try:
                self._execution_order = int(cpp_component.execution_order)
            except Exception:
                pass
            try:
                self._enabled = bool(cpp_component.enabled)
            except Exception:
                pass

    def _sync_native_state(
        self,
        enabled: bool,
        awake_called: bool,
        has_started: bool,
        is_destroyed: bool,
        execution_order: int,
    ):
        """Mirror native lifecycle state into Python for diagnostics and tooling."""
        self._enabled = bool(enabled)
        self._awake_called = bool(awake_called)
        self._has_started = bool(has_started)
        self._is_destroyed = bool(is_destroyed)
        self._execution_order = int(execution_order)

    def _invalidate_native_binding(self):
        """Invalidate native references after scene rebuild/destruction."""
        self._cpp_component = None
        self._enabled = False
        self._awake_called = False
        self._has_started = False
        self._is_destroyed = True
        self._remove_from_active_registry()
        self._game_object = None
        self._game_object_ref = None
        self._invalidate_cache()

    def _get_bound_native_component(self):
        """Return the native component if still alive, otherwise invalidate it."""
        cpp_component = getattr(self, '_cpp_component', None)
        if cpp_component is None:
            return None
        try:
            cpp_component.component_id
        except Exception:
            self._invalidate_native_binding()
            return None
        return cpp_component
    
    def _invalidate_cache(self):
        """Invalidate the component lookup cache."""
        self._component_cache.clear()
        self._cache_valid = False

    # ------------------------------------------------------------------
    # Internal helper: safely call a user-overridden lifecycle method,
    # routing any exception to the engine Console so it is visible in
    # the editor (not just stderr).
    # ------------------------------------------------------------------
    def _safe_lifecycle_call(self, method_name: str, *args):
        """Call *method_name* on self, catching and logging any exception."""
        try:
            getattr(self, method_name)(*args)
        except Exception as exc:
            # Route to DebugConsole so the Console Panel shows the error.
            try:
                from InfEngine.debug import debug
                debug.log_exception(exc, context=self)
            except Exception:
                # Absolute fallback if debug itself cannot be imported.
                import traceback
                traceback.print_exc()
    
    def _call_awake(self):
        """Internal: Trigger awake lifecycle."""
        if self._awake_called:
            return
        self._awake_called = True
        self._safe_lifecycle_call("awake")
    
    def _call_start(self):
        """Internal: Trigger start lifecycle if not already called."""
        if self._has_started:
            return
        self._has_started = True
        self._safe_lifecycle_call("start")
    
    def _call_update(self, delta_time: float):
        """Internal: Trigger update lifecycle."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("update", delta_time)
        # Tick coroutines after user update (matching Unity order)
        if self._coroutine_scheduler is not None:
            try:
                from InfEngine.timing import Time
                scaled_dt = Time.delta_time
            except Exception:
                scaled_dt = delta_time
            self._coroutine_scheduler.tick_update(scaled_dt)

    def _call_fixed_update(self, fixed_delta_time: float):
        """Internal: Trigger fixed_update lifecycle."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("fixed_update", fixed_delta_time)
        # Tick coroutines waiting for fixed_update
        if self._coroutine_scheduler is not None:
            self._coroutine_scheduler.tick_fixed_update(fixed_delta_time)

    def _call_late_update(self, delta_time: float):
        """Internal: Trigger late_update lifecycle."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("late_update", delta_time)
        # Tick coroutines waiting for end-of-frame
        if self._coroutine_scheduler is not None:
            try:
                from InfEngine.timing import Time
                scaled_dt = Time.delta_time
            except Exception:
                scaled_dt = delta_time
            self._coroutine_scheduler.tick_late_update(scaled_dt)
    
    @classmethod
    def _clear_all_instances(cls) -> None:
        """Clear the active-instances registry (call on scene unload/reload)."""
        cls._active_instances.clear()

    def _call_on_destroy(self):
        """Internal: Trigger on_destroy lifecycle."""
        if self._is_destroyed:
            return  # Already destroyed, don't call again
        self._is_destroyed = True
        self._enabled = False
        # Stop all coroutines before on_destroy callback
        if self._coroutine_scheduler is not None:
            self._coroutine_scheduler.stop_all()
            self._coroutine_scheduler = None
        # Remove from active-instances registry (safety net; _set_game_object(None)
        # should have done this already, but guard against missed calls)
        self._remove_from_active_registry()
        self._safe_lifecycle_call("on_destroy")
        # Clear references to help garbage collection
        self._cpp_component = None
        self._game_object = None
        self._game_object_ref = None
        self._component_cache.clear()

    def _call_on_enable(self):
        """Internal: Trigger on_enable lifecycle."""
        self._enabled = True
        self._safe_lifecycle_call("on_enable")

    def _call_on_disable(self):
        """Internal: Trigger on_disable lifecycle."""
        self._enabled = False
        self._safe_lifecycle_call("on_disable")

    def _call_on_validate(self):
        """Internal: Trigger on_validate lifecycle (editor only)."""
        self._safe_lifecycle_call("on_validate")

    def _call_reset(self):
        """Internal: Trigger reset lifecycle (editor only)."""
        self._safe_lifecycle_call("reset")

    def _call_on_after_deserialize(self):
        """Internal: Trigger on_after_deserialize lifecycle."""
        self._safe_lifecycle_call("on_after_deserialize")

    def _call_on_before_serialize(self):
        """Internal: Trigger on_before_serialize lifecycle."""
        self._safe_lifecycle_call("on_before_serialize")

    # ------------------------------------------------------------------
    # Physics collision / trigger callbacks (Unity-style)
    # ------------------------------------------------------------------

    def _call_on_collision_enter(self, collision):
        """Internal: Trigger on_collision_enter lifecycle."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("on_collision_enter", collision)

    def _call_on_collision_stay(self, collision):
        """Internal: Trigger on_collision_stay lifecycle."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("on_collision_stay", collision)

    def _call_on_collision_exit(self, collision):
        """Internal: Trigger on_collision_exit lifecycle."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("on_collision_exit", collision)

    def _call_on_trigger_enter(self, other):
        """Internal: Trigger on_trigger_enter lifecycle."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("on_trigger_enter", other)

    def _call_on_trigger_stay(self, other):
        """Internal: Trigger on_trigger_stay lifecycle."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("on_trigger_stay", other)

    def _call_on_trigger_exit(self, other):
        """Internal: Trigger on_trigger_exit lifecycle."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("on_trigger_exit", other)

    def _call_on_draw_gizmos(self):
        """Internal: Trigger on_draw_gizmos lifecycle (editor only)."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("on_draw_gizmos")

    def _call_on_draw_gizmos_selected(self):
        """Internal: Trigger on_draw_gizmos_selected lifecycle (editor only)."""
        if not self.enabled:
            return
        self._safe_lifecycle_call("on_draw_gizmos_selected")

    # ========================================================================
    # Lifecycle methods (override in subclasses)
    # ========================================================================
    
    def awake(self):
        """
        Called when the component is first added to a GameObject.
        Use for initialization that doesn't depend on other components.
        """
        pass
    
    def start(self):
        """
        Called before the first update, after all awake() calls.
        Use for initialization that depends on other components being ready.
        """
        pass
    
    def update(self, delta_time: float):
        """
        Called every frame.
        
        Args:
            delta_time: Time in seconds since last frame
        """
        pass

    def fixed_update(self, fixed_delta_time: float):
        """
        Called at a fixed time step (default 50 Hz).
        Use for physics and deterministic logic.
        
        Args:
            fixed_delta_time: Fixed time step in seconds
        """
        pass

    def late_update(self, delta_time: float):
        """
        Called every frame after all update() calls.
        Useful for camera follow, physics cleanup, etc.
        
        Args:
            delta_time: Time in seconds since last frame
        """
        pass
    
    def on_destroy(self):
        """
        Called when the component is being destroyed.
        Use for cleanup (unsubscribe events, release resources).
        """
        pass

    def on_enable(self):
        """
        Called when the component becomes enabled.
        Use for subscribing to events, starting coroutines, etc.
        """
        pass

    def on_disable(self):
        """
        Called when the component becomes disabled.
        Use for unsubscribing from events, stopping coroutines, etc.
        """
        pass

    def on_validate(self):
        """
        Called in the editor when the component is loaded or a value changes.
        Use for validation and clamping of serialized values.
        NOTE: This is editor-only; do not use for gameplay logic.
        """
        pass

    def reset(self):
        """
        Called in the editor when the user selects "Reset" on the component.
        Use to restore default values.
        NOTE: This is editor-only; do not use for gameplay logic.
        """
        pass

    def on_after_deserialize(self):
        """
        Called after component fields have been deserialized.
        Use to rebuild runtime references that weren't serialized.
        
        Example:
            def on_after_deserialize(self):
                # Rebuild cached references
                self._cached_renderer = self.get_component(MeshRenderer)
        """
        pass

    def on_before_serialize(self):
        """
        Called before component fields are serialized.
        Use to prepare data for serialization.
        """
        pass

    # ========================================================================
    # Physics collision / trigger callbacks (Unity-style)
    # ========================================================================

    def on_collision_enter(self, collision):
        """
        Called when this collider starts touching another collider.

        Args:
            collision: CollisionInfo with contact details (collider,
                       game_object, contact_point, contact_normal,
                       relative_velocity, impulse).
        """
        pass

    def on_collision_stay(self, collision):
        """
        Called every fixed-update while two colliders remain in contact.

        Args:
            collision: CollisionInfo with contact details.
        """
        pass

    def on_collision_exit(self, collision):
        """
        Called when two colliders stop touching.

        Args:
            collision: CollisionInfo with contact details.
        """
        pass

    def on_trigger_enter(self, other):
        """
        Called when another collider enters this trigger volume.

        Args:
            other: The other Collider that entered.
        """
        pass

    def on_trigger_stay(self, other):
        """
        Called every fixed-update while another collider is inside this trigger.

        Args:
            other: The other Collider that is inside.
        """
        pass

    def on_trigger_exit(self, other):
        """
        Called when another collider exits this trigger volume.

        Args:
            other: The other Collider that exited.
        """
        pass

    def on_draw_gizmos(self):
        """
        Called every frame in the editor to draw gizmos for this component.

        Override this to draw custom visual aids (wireframes, lines, etc.)
        using the ``Gizmos`` API.  When ``always_show`` is False on this
        component, this callback is only invoked when the owning
        GameObject (or one of its ancestors) is selected.

        Example::

            from InfEngine.gizmos import Gizmos

            def on_draw_gizmos(self):
                Gizmos.color = (0, 1, 0)
                Gizmos.draw_wire_sphere(self.transform.position, 2.0)
        """
        pass

    def on_draw_gizmos_selected(self):
        """
        Called every frame in the editor ONLY when this object is selected.

        Use this for gizmos that should only appear when the user is
        inspecting this specific object.

        Example::

            from InfEngine.gizmos import Gizmos

            def on_draw_gizmos_selected(self):
                Gizmos.color = (1, 1, 0)
                Gizmos.draw_wire_cube(self.transform.position, (1, 1, 1))
        """
        pass

    # ========================================================================
    # Coroutine support (Unity-style)
    # ========================================================================

    def start_coroutine(self, generator) -> 'Coroutine':
        """Start a coroutine on this component.

        Args:
            generator: A generator object (call your generator function first).

        Returns:
            A :class:`~InfEngine.coroutine.Coroutine` handle that can be passed
            to :meth:`stop_coroutine` or ``yield``-ed from another coroutine to
            wait for completion.

        Example::

            from InfEngine.coroutine import WaitForSeconds

            class Enemy(InfComponent):
                def start(self):
                    self.start_coroutine(self.patrol())

                def patrol(self):
                    while True:
                        debug.log("Moving left")
                        yield WaitForSeconds(2)
                        debug.log("Moving right")
                        yield WaitForSeconds(2)
        """
        from InfEngine.coroutine import CoroutineScheduler
        if self._coroutine_scheduler is None:
            self._coroutine_scheduler = CoroutineScheduler()
        return self._coroutine_scheduler.start(generator, owner=self)

    def stop_coroutine(self, coroutine) -> None:
        """Stop a specific coroutine previously started with :meth:`start_coroutine`.

        Args:
            coroutine: The :class:`~InfEngine.coroutine.Coroutine` handle.
        """
        if self._coroutine_scheduler is not None:
            self._coroutine_scheduler.stop(coroutine)

    def stop_all_coroutines(self) -> None:
        """Stop **all** coroutines running on this component."""
        if self._coroutine_scheduler is not None:
            self._coroutine_scheduler.stop_all()

    # ========================================================================
    # Serialization (used by Play Mode snapshot)
    # ========================================================================
    
    def _serialize_fields(self) -> str:
        """
        Serialize all serialized fields to JSON string.
        Called by C++ PyComponentProxy::Serialize().
        
        Returns:
            JSON string of field values
        """
        import json
        from .serialized_field import get_serialized_fields
        
        # Call on_before_serialize hook
        self._call_on_before_serialize()
        
        fields = get_serialized_fields(self.__class__)
        data = {
            "__schema_version__": getattr(self, "__schema_version__", 1),
            "__type_name__": self.__class__.__name__,
            "__component_id__": self._component_id,
        }
        for name, meta in fields.items():
            value = getattr(self, name, meta.default)
            data[name] = self._serialize_value(value)
        
        return json.dumps(data)
    
    def _deserialize_fields(self, json_str: str):
        """
        Restore serialized field values from JSON string.
        Calls on_after_deserialize() after restoration.
        
        Args:
            json_str: JSON string of field values
        """
        import json
        from .serialized_field import get_serialized_fields
        
        data = json.loads(json_str)
        schema_version = data.get("__schema_version__", None)
        if schema_version is not None and schema_version != getattr(self, "__schema_version__", 1):
            from InfEngine.debug import Debug
            Debug.log_warning(
                f"Component schema mismatch: {self.__class__.__name__} "
                f"(saved={schema_version}, current={getattr(self, '__schema_version__', 1)})"
            )

        # Restore component ID if present
        saved_id = data.get("__component_id__")
        if saved_id is not None:
            self._component_id = int(saved_id)
            # Ensure ID generator is ahead of restored ID
            with InfComponent._id_lock:
                if InfComponent._next_component_id <= self._component_id:
                    InfComponent._next_component_id = self._component_id + 1

        fields = get_serialized_fields(self.__class__)

        for name, value in data.items():
            if name.startswith("__"):
                continue
            if name in fields:
                meta = fields[name]
                value = self._deserialize_value(value, meta.field_type)
                setattr(self, name, value)

        # Call on_after_deserialize hook
        self._call_on_after_deserialize()


    def _serialize_value(self, value: Any):
        """Serialize a value into JSON-friendly format."""
        if isinstance(value, (bool, int, float, str, type(None))):
            return value

        # Recursively serialize list/dict so that nested refs/enums survive
        if isinstance(value, list):
            return [self._serialize_value(item) for item in value]
        if isinstance(value, dict):
            return {k: self._serialize_value(v) for k, v in value.items()}

        # Enum support: store type name + member name
        from enum import Enum as _Enum
        if isinstance(value, _Enum):
            return {"__enum__": type(value).__qualname__, "name": value.name}

        # GameObjectRef (null-safe wrapper) — store persistent ID
        from InfEngine.components.ref_wrappers import GameObjectRef, MaterialRef
        if isinstance(value, GameObjectRef):
            return {"__game_object__": value.persistent_id}

        # MaterialRef (null-safe wrapper) — store GUID + file_path fallback
        if isinstance(value, MaterialRef):
            d = {"__material_guid__": value.guid}
            if value._file_path:
                d["__file_path__"] = value._file_path
            return d

        # TextureRef — store GUID + path_hint
        from InfEngine.core.asset_ref import TextureRef, ShaderRef
        if isinstance(value, TextureRef):
            d: dict = {"__texture_ref__": value.guid}
            if value.path_hint:
                d["__path_hint__"] = value.path_hint
            return d

        # ShaderRef — store GUID + path_hint
        if isinstance(value, ShaderRef):
            d = {"__shader_ref__": value.guid}
            if value.path_hint:
                d["__path_hint__"] = value.path_hint
            return d

        # Raw GameObject reference — store persistent ID (scene-stable)
        if hasattr(value, 'id') and hasattr(value, 'name') and hasattr(value, 'transform'):
            return {"__game_object__": int(value.id)}

        # Raw Material reference — store GUID via AssetDatabase
        try:
            from InfEngine.core.material import Material
            if isinstance(value, Material):
                guid = MaterialRef._extract_guid(value)
                if guid:
                    return {"__material_guid__": guid}
                # Fallback: store file_path if no GUID available
                path = getattr(value.native, 'file_path', '') if value.native else ''
                return {"__material_guid__": path or value.name}
        except ImportError:
            pass

        # Vec2/3/4 support (pybind types expose x/y/z/w)
        if hasattr(value, "x") and hasattr(value, "y") and hasattr(value, "z") and hasattr(value, "w"):
            return [float(value.x), float(value.y), float(value.z), float(value.w)]
        if hasattr(value, "x") and hasattr(value, "y") and hasattr(value, "z"):
            return [float(value.x), float(value.y), float(value.z)]
        if hasattr(value, "x") and hasattr(value, "y"):
            return [float(value.x), float(value.y)]

        # Fallback: string representation
        return str(value)

    def _deserialize_value(self, value: Any, field_type):
        """Deserialize a value based on FieldType."""
        from InfEngine.components.serialized_field import FieldType
        from InfEngine.math import vec2f, vec3f, vec4f

        # Null values for ref types → return a null ref wrapper (not raw None)
        if value is None:
            if field_type == FieldType.GAME_OBJECT:
                from InfEngine.components.ref_wrappers import GameObjectRef
                return GameObjectRef(persistent_id=0)
            if field_type == FieldType.MATERIAL:
                from InfEngine.components.ref_wrappers import MaterialRef
                return MaterialRef(guid="")
            if field_type == FieldType.TEXTURE:
                from InfEngine.core.asset_ref import TextureRef
                return TextureRef()
            if field_type == FieldType.SHADER:
                from InfEngine.core.asset_ref import ShaderRef
                return ShaderRef()
            return None

        if field_type == FieldType.VEC2:
            return self._to_vec(value, 2, vec2f)
        if field_type == FieldType.VEC3:
            return self._to_vec(value, 3, vec3f)
        if field_type == FieldType.VEC4:
            return self._to_vec(value, 4, vec4f)
        if field_type == FieldType.ENUM and isinstance(value, dict) and "__enum__" in value:
            return self._deserialize_enum(value)

        # GAME_OBJECT: resolve by persistent ID → GameObjectRef (null-safe)
        if field_type == FieldType.GAME_OBJECT and isinstance(value, dict) and "__game_object__" in value:
            from InfEngine.components.ref_wrappers import GameObjectRef
            obj_id = int(value["__game_object__"])
            return GameObjectRef(persistent_id=obj_id)

        # MATERIAL: resolve by GUID → MaterialRef (null-safe)
        if field_type == FieldType.MATERIAL and isinstance(value, dict) and "__material_guid__" in value:
            from InfEngine.components.ref_wrappers import MaterialRef
            guid = value["__material_guid__"]
            file_path = value.get("__file_path__", "")
            return MaterialRef(guid=guid, file_path=file_path)

        # TEXTURE: resolve by GUID → TextureRef
        if field_type == FieldType.TEXTURE and isinstance(value, dict) and "__texture_ref__" in value:
            from InfEngine.core.asset_ref import TextureRef
            guid = value["__texture_ref__"]
            path_hint = value.get("__path_hint__", "")
            return TextureRef(guid=guid, path_hint=path_hint)

        # SHADER: resolve by GUID → ShaderRef
        if field_type == FieldType.SHADER and isinstance(value, dict) and "__shader_ref__" in value:
            from InfEngine.core.asset_ref import ShaderRef
            guid = value["__shader_ref__"]
            path_hint = value.get("__path_hint__", "")
            return ShaderRef(guid=guid, path_hint=path_hint)

        return value

    def _deserialize_enum(self, value: dict):
        """Reconstruct an enum member from {__enum__, name}."""
        from .serialized_field import get_serialized_fields
        fields = get_serialized_fields(self.__class__)
        for meta in fields.values():
            if meta.enum_type is not None and meta.enum_type.__qualname__ == value["__enum__"]:
                return meta.enum_type[value["name"]]
        return value  # fallback: return dict as-is

    def _to_vec(self, value: Any, n: int, ctor):
        """Convert list/tuple/string to vec type if possible."""
        if isinstance(value, (list, tuple)) and len(value) >= n:
            return ctor(*[float(value[i]) for i in range(n)])
        if isinstance(value, str):
            cleaned = value.strip().replace("<", "").replace(">", "").replace("(", "").replace(")", "")
            parts = [p for p in cleaned.split(",") if p.strip()]
            if len(parts) >= n:
                nums = [float(p.strip()) for p in parts[:n]]
                return ctor(*nums)
        return value
    
    # ========================================================================
    # Utility methods
    # ========================================================================
    
    def get_component(self, component_type) -> Optional[Any]:
        """
        Get another component of the specified type on the same GameObject.
        
        Supports BuiltinComponent wrappers (Light, MeshRenderer, Camera),
        Python InfComponent subclasses, and raw C++ types (Transform).
        
        Args:
            component_type: The component type to find. Can be:
                - A BuiltinComponent subclass (Light, MeshRenderer, Camera)
                - A Python InfComponent subclass
                - A string type name (e.g., "MeshRenderer", "PlayerController")
            
        Returns:
            The component instance, or None if not found
            
        Example:
            # Get built-in component by class
            from InfEngine.components import Light, MeshRenderer, Camera
            light = self.get_component(Light)
            
            # Get Python component by class
            player = self.get_component(PlayerController)
            
            # Get component by string name
            mesh = self.get_component("MeshRenderer")
            player = self.get_component("PlayerController")
        """
        if self._is_destroyed:
            return None
        
        go = self.game_object
        if go is None:
            return None
        
        # Determine the type name for lookup
        type_name: Optional[str] = None
        resolved_class = None  # Python class resolved from registry
        
        # ----- BuiltinComponent fast path -----
        # Detect BuiltinComponent subclass (by class or string) and wrap
        # the raw C++ component in a Python wrapper.
        from .builtin_component import BuiltinComponent

        builtin_cls = None  # resolved BuiltinComponent subclass (if any)

        if isinstance(component_type, type) and issubclass(component_type, BuiltinComponent):
            builtin_cls = component_type
            type_name = getattr(component_type, '_cpp_type_name', '') or component_type.__name__
        elif isinstance(component_type, str):
            # Check if a BuiltinComponent wrapper exists for this string
            builtin_cls = BuiltinComponent._builtin_registry.get(component_type)
            type_name = component_type
        elif hasattr(component_type, '__name__'):
            builtin_cls = BuiltinComponent._builtin_registry.get(component_type.__name__)
            type_name = component_type.__name__

        if builtin_cls is not None:
            # Look up the C++ component and wrap it
            cpp_type = getattr(builtin_cls, '_cpp_type_name', type_name)
            cpp_comp = go.get_cpp_component(cpp_type)
            if cpp_comp is not None:
                return builtin_cls._get_or_create_wrapper(cpp_comp, go)
            return None

        # ----- Python / fallback lookup -----
        resolved_class = None

        if isinstance(component_type, str):
            type_name = component_type
            # Try Python registry first
            from .registry import get_type
            resolved_class = get_type(type_name)
        elif isinstance(component_type, type):
            type_name = component_type.__name__
            if issubclass(component_type, InfComponent):
                resolved_class = component_type
        elif hasattr(component_type, '__name__'):
            type_name = component_type.__name__
        
        # Check cache (Python components only)
        cache_key = resolved_class if resolved_class else component_type
        if self._cache_valid and cache_key in self._component_cache:
            cached = self._component_cache[cache_key]
            if cached is not None and not getattr(cached, '_is_destroyed', False):
                return cached
        
        if resolved_class is not None:
            result = go.get_py_component(resolved_class)
            self._component_cache[cache_key] = result
            self._cache_valid = True
            return result
        elif isinstance(component_type, str):
            # Try Python component by class name
            for comp in go.get_py_components():
                if comp.__class__.__name__ == component_type:
                    self._component_cache[component_type] = comp
                    self._cache_valid = True
                    return comp
            # Fall back to raw C++ lookup (e.g. "Transform")
            return go.get_cpp_component(type_name)
        else:
            # Raw pybind11 class or unknown — try Python first, then C++
            result = go.get_py_component(component_type)
            if result is not None:
                self._component_cache[component_type] = result
                self._cache_valid = True
                return result
            if type_name:
                return go.get_cpp_component(type_name)
            return None
    
    def add_component(self, component_type):
        """
        Add a component to this component's GameObject.

        Supports BuiltinComponent subclasses (Light, MeshRenderer, Camera),
        InfComponent subclasses (user scripts), and raw C++ type names.

        Unity equivalent: ``gameObject.AddComponent<T>()``

        Args:
            component_type: The component to add. Can be:
                - A BuiltinComponent subclass (Light, MeshRenderer, Camera)
                - An InfComponent subclass (user-defined script)
                - A string (C++ type name, e.g. ``"MeshRenderer"``)

        Returns:
            The added component instance, or None on failure.

        Example::

            light = self.add_component(Light)
            light.intensity = 2.0
        """
        if self._is_destroyed:
            return None
        go = self.game_object
        if go is None:
            return None

        from .builtin_component import BuiltinComponent

        # BuiltinComponent subclass → create C++ component + Python wrapper
        if isinstance(component_type, type) and issubclass(component_type, BuiltinComponent):
            cpp_type_name = getattr(component_type, '_cpp_type_name', '') or component_type.__name__
            cpp_comp = go.add_component(cpp_type_name)
            if cpp_comp is None:
                return None
            return component_type._get_or_create_wrapper(cpp_comp, go)

        # InfComponent subclass → create Python instance + PyComponentProxy
        if isinstance(component_type, type) and issubclass(component_type, InfComponent):
            instance = component_type()
            return go.add_py_component(instance)

        # String or raw C++ type → delegate to C++ binding
        return go.add_component(component_type)

    def get_components(self, component_type) -> List[Any]:
        """
        Get all components of the specified type on the same GameObject.
        
        Args:
            component_type: The component type to find. Can be:
                - A BuiltinComponent subclass (Light, MeshRenderer, Camera)
                - A Python InfComponent subclass
                - A string type name
            
        Returns:
            List of component instances (may be empty)
        """
        if self._is_destroyed:
            return []
        
        go = self.game_object
        if go is None:
            return []

        # BuiltinComponent fast-path
        from .builtin_component import BuiltinComponent
        builtin_cls = None
        if isinstance(component_type, type) and issubclass(component_type, BuiltinComponent):
            builtin_cls = component_type
        elif isinstance(component_type, str):
            builtin_cls = BuiltinComponent._builtin_registry.get(component_type)
        elif hasattr(component_type, '__name__'):
            builtin_cls = BuiltinComponent._builtin_registry.get(component_type.__name__)

        if builtin_cls is not None:
            cpp_type = getattr(builtin_cls, '_cpp_type_name', '')
            cpp_list = go.get_cpp_components(cpp_type)
            return [builtin_cls._get_or_create_wrapper(c, go) for c in cpp_list]

        # Python / fallback
        if isinstance(component_type, str):
            # Try Python component by class name
            result = [c for c in go.get_py_components()
                      if c.__class__.__name__ == component_type]
            if result:
                return result
            # Fall back to raw C++ lookup
            return go.get_cpp_components(component_type)
        elif isinstance(component_type, type) and issubclass(component_type, InfComponent):
            return [c for c in go.get_py_components()
                    if isinstance(c, component_type)]
        else:
            # Raw pybind11 class — try C++
            type_name = getattr(component_type, '__name__', '')
            if type_name:
                return go.get_cpp_components(type_name)
            return []

    def get_component_in_children(self, component_type, include_inactive: bool = False) -> Optional[Any]:
        """
        Get a component of the specified type on this or any child GameObject.
        
        Searches self first, then recursively through all children.
        Supports both Python components (InfComponent subclasses) and C++ components.
        
        Unity equivalent: GetComponentInChildren<T>(bool includeInactive = false)
        
        Args:
            component_type: The component type to find (class, string, or C++ type)
            include_inactive: If True, also search inactive GameObjects (default: False)
            
        Returns:
            The component instance, or None if not found
            
        Example:
            renderer = self.get_component_in_children(MeshRenderer)
            controller = self.get_component_in_children(PlayerController, include_inactive=True)
        """
        if self._is_destroyed:
            return None
        go = self.game_object
        if go is None:
            return None

        # BuiltinComponent wrapping
        from .builtin_component import BuiltinComponent
        builtin_cls = None
        if isinstance(component_type, type) and issubclass(component_type, BuiltinComponent):
            builtin_cls = component_type
        elif isinstance(component_type, str):
            builtin_cls = BuiltinComponent._builtin_registry.get(component_type)

        result = go.get_component_in_children(component_type, include_inactive)
        if result is not None and builtin_cls is not None:
            result_go = getattr(result, 'game_object', go)
            return builtin_cls._get_or_create_wrapper(result, result_go)
        return result

    def get_component_in_parent(self, component_type, include_inactive: bool = False) -> Optional[Any]:
        """
        Get a component of the specified type on this or any parent GameObject.
        
        Searches self first, then walks up the parent hierarchy.
        Supports both Python components (InfComponent subclasses) and C++ components.
        
        Unity equivalent: GetComponentInParent<T>(bool includeInactive = false)
        
        Args:
            component_type: The component type to find (class, string, or C++ type)
            include_inactive: If True, also search inactive GameObjects (default: False)
            
        Returns:
            The component instance, or None if not found
            
        Example:
            camera = self.get_component_in_parent(Camera)
            manager = self.get_component_in_parent(GameManager)
        """
        if self._is_destroyed:
            return None
        go = self.game_object
        if go is None:
            return None

        # BuiltinComponent wrapping
        from .builtin_component import BuiltinComponent
        builtin_cls = None
        if isinstance(component_type, type) and issubclass(component_type, BuiltinComponent):
            builtin_cls = component_type
        elif isinstance(component_type, str):
            builtin_cls = BuiltinComponent._builtin_registry.get(component_type)

        result = go.get_component_in_parent(component_type, include_inactive)
        if result is not None and builtin_cls is not None:
            result_go = getattr(result, 'game_object', go)
            return builtin_cls._get_or_create_wrapper(result, result_go)
        return result

    def try_get_component(self, component_type) -> tuple:
        """
        Try to get a component of the specified type.
        Unity-style TryGetComponent pattern.
        
        Supports both Python components and C++ components.
        
        Args:
            component_type: The type of component to find
            
        Returns:
            Tuple of (success: bool, component: Optional[Component])
        """
        comp = self.get_component(component_type)
        return (comp is not None, comp)
    
    def get_mesh_renderer(self):
        """
        Convenience method to get the MeshRenderer component on this GameObject.
        
        Returns:
            MeshRenderer wrapper or None if not found
        """
        from .builtin import MeshRenderer
        return self.get_component(MeshRenderer)

    # ========================================================================
    # Tag & Layer convenience properties
    # ========================================================================

    @property
    def tag(self) -> str:
        """Get the tag of the attached GameObject."""
        go = self.game_object
        if go is not None and hasattr(go, 'tag'):
            return go.tag
        return "Untagged"

    @tag.setter
    def tag(self, value: str):
        """Set the tag of the attached GameObject."""
        go = self.game_object
        if go is not None and hasattr(go, 'tag'):
            go.tag = value

    @property
    def game_object_layer(self) -> int:
        """Get the layer of the attached GameObject."""
        go = self.game_object
        if go is not None and hasattr(go, 'layer'):
            return go.layer
        return 0

    @game_object_layer.setter
    def game_object_layer(self, value: int):
        """Set the layer of the attached GameObject."""
        go = self.game_object
        if go is not None and hasattr(go, 'layer'):
            go.layer = value

    def compare_tag(self, tag: str) -> bool:
        """Returns True if the attached GameObject's tag matches the given tag."""
        go = self.game_object
        if go is not None and hasattr(go, 'compare_tag'):
            return go.compare_tag(tag)
        return False

    def __repr__(self) -> str:
        return f"<{self._component_name} id={self._component_id} enabled={self.enabled}>"
