"""
BuiltinComponent — Base class for Python wrappers around C++ built-in components.

Provides a unified InfComponent interface for C++ components like Light,
MeshRenderer, and Camera.  The actual state lives in the C++ component;
Python CppProperty descriptors delegate reads/writes transparently.

Design:
    - BuiltinComponent inherits from InfComponent (unified type system)
    - No PyComponentProxy is created (C++ component is already in m_components)
    - Properties delegate to the C++ component via CppProperty descriptors
    - Wrappers are cached per component_id for identity stability

Usage (from within an InfComponent script)::

    from InfEngine.components.builtin import Light, MeshRenderer

    class MyScript(InfComponent):
        def start(self):
            light = self.get_component(Light)
            light.intensity = 2.0

        def update(self, dt):
            mr = self.get_component(MeshRenderer)
            if mr:
                mr.casts_shadows = False
"""

from __future__ import annotations

import weakref
from typing import Any, Dict, Optional, Type, TYPE_CHECKING

from .serialized_field import FieldMetadata, FieldType
from .component import InfComponent

if TYPE_CHECKING:
    from InfEngine.lib import Component as CppComponent, GameObject


# =============================================================================
# CppProperty — descriptor that delegates to a C++ component attribute
# =============================================================================


class CppProperty:
    """Descriptor that delegates get/set to a C++ component property.

    Used by BuiltinComponent subclasses to expose C++ properties as
    serialized fields within the InfComponent system.

    The ``_is_cpp_property`` flag allows InfComponent.__init_subclass__
    to recognise this descriptor without importing this module (no
    circular dependency).

    Args:
        cpp_attr: Attribute name on the pybind11 C++ component object.
        field_type: FieldType for Inspector rendering.
        default: Fallback value when the C++ component is not yet bound.
        readonly: If ``True``, the property cannot be set from Python.
        tooltip: Hover text for the Inspector panel.
        header: Group header shown above this field in the Inspector.
        range: ``(min, max)`` tuple for numeric slider widgets.
        enum_type: Enum class for ENUM fields.

    Example::

        class Light(BuiltinComponent):
            _cpp_type_name = "Light"
            intensity = CppProperty("intensity", FieldType.FLOAT, default=1.0)
    """

    _is_cpp_property: bool = True  # marker for InfComponent.__init_subclass__

    def __init__(
        self,
        cpp_attr: str,
        field_type: FieldType = FieldType.UNKNOWN,
        default: Any = None,
        *,
        readonly: bool = False,
        tooltip: str = "",
        header: str = "",
        range: Optional[tuple] = None,
        enum_type=None,
        enum_labels: Optional[list] = None,
        visible_when=None,
    ):
        self.cpp_attr = cpp_attr
        self.metadata = FieldMetadata(
            name="",  # filled by __set_name__ / __init_subclass__
            field_type=field_type,
            default=default,
            readonly=readonly,
            tooltip=tooltip,
            header=header,
            range=range,
            enum_type=enum_type,
            enum_labels=enum_labels,
            visible_when=visible_when,
        )

    # Called by Python when the class body is processed.
    def __set_name__(self, owner: type, name: str):
        if not self.metadata.name:
            self.metadata.name = name

    def __get__(self, instance: Optional[Any], owner: type) -> Any:
        if instance is None:
            return self
        cpp = getattr(instance, "_cpp_component", None)
        if cpp is not None:
            return getattr(cpp, self.cpp_attr)
        return self.metadata.default

    def __set__(self, instance: Any, value: Any) -> None:
        if self.metadata.readonly:
            raise AttributeError(
                f"Property '{self.metadata.name}' is read-only"
            )
        cpp = getattr(instance, "_cpp_component", None)
        if cpp is not None:
            setattr(cpp, self.cpp_attr, value)


# =============================================================================
# BuiltinComponent — base class
# =============================================================================


class BuiltinComponent(InfComponent):
    """Base class for Python wrappers around C++ built-in components.

    Subclasses MUST set ``_cpp_type_name`` to the C++ component's registered
    type name (e.g. ``"Light"``, ``"MeshRenderer"``, ``"Camera"``).

    The wrapper is fully compatible with the InfComponent type system:

    * ``isinstance(wrapper, InfComponent)`` → ``True``
    * ``get_component(Light)`` returns the wrapper
    * Inspector reads CppProperty metadata for field display
    * Serialisation delegates to the C++ component

    Lifecycle note:
        Because the underlying C++ component already participates in the
        C++ update loop, BuiltinComponent does **not** create a
        ``PyComponentProxy``.  Lifecycle methods (awake/start/update …)
        are inherited from InfComponent and can be overridden, but they
        are **not** called automatically by the C++ loop.  If you need
        per-frame behaviour on a built-in component, attach a separate
        InfComponent script instead — this mirrors Unity's pattern where
        ``Light``/``Camera``/``MeshRenderer`` are ``Component`` (not
        ``MonoBehaviour``).
    """

    # ---- Must be overridden in concrete subclasses ----
    _cpp_type_name: str = ""

    # ---- Instance state (set by _bind_cpp) ----
    _cpp_component: Optional[Any] = None  # pybind11 C++ component reference

    # ---- Class-level registries ----
    _builtin_registry: Dict[str, Type["BuiltinComponent"]] = {}
    _wrapper_cache: weakref.WeakValueDictionary = weakref.WeakValueDictionary()

    # ------------------------------------------------------------------
    # Metaclass hook — register concrete subclasses automatically
    # ------------------------------------------------------------------

    def __init_subclass__(cls, **kwargs):
        # InfComponent.__init_subclass__ runs first via super() chain and
        # processes CppProperty descriptors (thanks to _is_cpp_property).
        super().__init_subclass__(**kwargs)

        cpp_name = getattr(cls, "_cpp_type_name", "")
        if cpp_name and cpp_name != BuiltinComponent._cpp_type_name:
            BuiltinComponent._builtin_registry[cpp_name] = cls

    # ------------------------------------------------------------------
    # Binding / wrapping
    # ------------------------------------------------------------------

    def _bind_cpp(
        self, cpp_component: "CppComponent", game_object: "GameObject"
    ) -> None:
        """Bind this Python wrapper to an existing C++ component.

        Called after ``cls()`` construction to link the wrapper to the
        underlying C++ object.  Syncs component ID, enabled state, and
        game_object reference.
        """
        self._bind_native_component(cpp_component, game_object)
        # Cache by component_id
        cache_key = cpp_component.component_id
        BuiltinComponent._wrapper_cache[cache_key] = self

    @classmethod
    def _get_or_create_wrapper(
        cls, cpp_component: "CppComponent", game_object: "GameObject"
    ) -> "BuiltinComponent":
        """Return an existing wrapper or create a new one.

        The cache is keyed by the C++ component's stable ``component_id``
        so the same Python object is returned on repeated lookups.
        """
        comp_id = cpp_component.component_id
        existing = BuiltinComponent._wrapper_cache.get(comp_id)
        if existing is not None and existing.is_valid:
            return existing
        if existing is not None:
            existing._invalidate_native_binding()

        wrapper = cls()
        wrapper._bind_cpp(cpp_component, game_object)
        return wrapper

    @classmethod
    def _clear_cache(cls) -> None:
        """Clear the wrapper cache (call on scene change / play-mode stop)."""
        for wrapper in list(BuiltinComponent._wrapper_cache.values()):
            try:
                wrapper._invalidate_native_binding()
            except Exception:
                pass
        BuiltinComponent._wrapper_cache.clear()

    # ------------------------------------------------------------------
    # Property overrides (delegate to C++)
    # ------------------------------------------------------------------

    @property  # type: ignore[override]
    def enabled(self) -> bool:
        cpp = self._get_bound_native_component()
        if cpp is not None:
            try:
                return bool(cpp.enabled)
            except Exception:
                self._invalidate_native_binding()
                return self._enabled
        return self._enabled

    @enabled.setter
    def enabled(self, value: bool) -> None:
        self._enabled = value
        cpp = self._get_bound_native_component()
        if cpp is not None:
            try:
                cpp.enabled = value
            except Exception:
                self._invalidate_native_binding()

    @property
    def is_valid(self) -> bool:
        return self._get_bound_native_component() is not None and not self._is_destroyed

    @property
    def component_id(self) -> int:
        cpp = self._get_bound_native_component()
        if cpp is not None:
            try:
                return cpp.component_id
            except Exception:
                self._invalidate_native_binding()
                return self._component_id
        return self._component_id

    # ------------------------------------------------------------------
    # Serialization (delegate to C++ component)
    # ------------------------------------------------------------------

    def _serialize_fields(self) -> str:
        """Serialize via the C++ component's own serializer."""
        cpp = self._get_bound_native_component()
        if cpp is not None:
            try:
                return cpp.serialize()
            except Exception:
                self._invalidate_native_binding()
        return "{}"

    def _deserialize_fields(self, json_str: str) -> None:
        """Deserialize via the C++ component."""
        cpp = self._get_bound_native_component()
        if cpp is not None:
            try:
                cpp.deserialize(json_str)
            except Exception:
                self._invalidate_native_binding()

    # ------------------------------------------------------------------
    # String representation
    # ------------------------------------------------------------------

    def __repr__(self) -> str:
        bound = self._get_bound_native_component() is not None
        return (
            f"<{self.__class__.__name__}"
            f" cpp={self._cpp_type_name}"
            f" bound={bound}"
            f" id={self._component_id}>"
        )
