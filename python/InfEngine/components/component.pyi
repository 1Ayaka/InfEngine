"""Type stubs for InfEngine.components.component — InfComponent base class."""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple, Union

from InfEngine.lib._InfEngine import (
    GameObject,
    MeshRenderer,
    Transform,
)


class InfComponent:
    """Base class for Python-scripted game components (Unity-style lifecycle).

    Subclass this to create game logic scripts. Use ``serialized_field()``
    class variables for Inspector-editable properties.

    Example::

        class PlayerController(InfComponent):
            speed: float = serialized_field(default=5.0, range=(0, 100))

            def start(self):
                Debug.log("PlayerController started")

            def update(self, delta_time: float):
                pos = self.transform.position
                self.transform.position = vec3f(
                    pos.x + self.speed * delta_time, pos.y, pos.z
                )
    """

    _serialized_fields_: Dict[str, Any]
    __schema_version__: int

    def __init__(self) -> None: ...

    # =========================================================================
    # Properties
    # =========================================================================

    @property
    def game_object(self) -> Optional[GameObject]:
        """The GameObject this component is attached to.

        Returns ``None`` before ``_set_game_object()`` or after destruction.
        """
        ...
    @property
    def transform(self) -> Optional[Transform]:
        """Shortcut to ``self.game_object.transform``.

        Returns ``None`` if the component has no valid ``game_object``.
        """
        ...
    @property
    def is_valid(self) -> bool:
        """Whether the underlying GameObject reference is still alive."""
        ...
    @property
    def enabled(self) -> bool:
        """Whether the component is enabled."""
        ...
    @enabled.setter
    def enabled(self, value: bool) -> None: ...
    @property
    def type_name(self) -> str:
        """Class name of this component."""
        ...
    @property
    def component_id(self) -> int:
        """Unique auto-incremented ID for this component instance."""
        ...

    # =========================================================================
    # Lifecycle methods — override in subclasses
    # =========================================================================

    def awake(self) -> None:
        """Called once when the component is first created."""
        ...
    def start(self) -> None:
        """Called before the first Update after the component is enabled."""
        ...
    def update(self, delta_time: float) -> None:
        """Called every frame."""
        ...
    def fixed_update(self, fixed_delta_time: float) -> None:
        """Called at a fixed time step (default 50 Hz). Use for physics."""
        ...
    def late_update(self, delta_time: float) -> None:
        """Called every frame after all Update calls."""
        ...
    def on_destroy(self) -> None:
        """Called when the component or its GameObject is destroyed."""
        ...
    def on_enable(self) -> None:
        """Called when the component is enabled."""
        ...
    def on_disable(self) -> None:
        """Called when the component is disabled."""
        ...
    def on_validate(self) -> None:
        """Called when a serialized field is changed in the Inspector."""
        ...
    def reset(self) -> None:
        """Called when the component is reset to defaults."""
        ...
    def on_after_deserialize(self) -> None:
        """Called after deserialization (scene load / undo)."""
        ...
    def on_before_serialize(self) -> None:
        """Called before serialization (scene save)."""
        ...

    # =========================================================================
    # Utility methods
    # =========================================================================

    def get_component(self, component_type: Union[type, str]) -> Optional[Any]:
        """Get another component of the specified type on the same GameObject.

        Args:
            component_type: A class (``Transform``, ``MeshRenderer``, or an
                ``InfComponent`` subclass) or a string type name.
        """
        ...
    def get_components(self, component_type: Union[type, str]) -> List[Any]:
        """Get all components of the specified type on the same GameObject."""
        ...
    def try_get_component(self, component_type: Union[type, str]) -> Tuple[bool, Optional[Any]]:
        """Try to get a component; returns (found, component_or_None)."""
        ...
    def get_mesh_renderer(self) -> Optional[MeshRenderer]:
        """Shortcut to get the MeshRenderer on the same GameObject."""
        ...
    def compare_tag(self, tag: str) -> bool:
        """Returns True if the attached GameObject's tag matches."""
        ...
    def get_component_in_children(
        self, component_type: Union[type, str], include_inactive: bool = ...,
    ) -> Optional[Any]:
        """Get a component of the specified type on this or any child GameObject.

        Args:
            component_type: Class or string name.
            include_inactive: Search inactive GameObjects too.
        """
        ...
    def get_component_in_parent(
        self, component_type: Union[type, str], include_inactive: bool = ...,
    ) -> Optional[Any]:
        """Get a component of the specified type on this or any parent GameObject.

        Args:
            component_type: Class or string name.
            include_inactive: Search inactive GameObjects too.
        """
        ...

    # =========================================================================
    # Tag & Layer convenience properties
    # =========================================================================

    @property
    def tag(self) -> str:
        """Tag of the attached GameObject."""
        ...
    @tag.setter
    def tag(self, value: str) -> None: ...
    @property
    def game_object_layer(self) -> int:
        """Layer index (0-31) of the attached GameObject."""
        ...
    @game_object_layer.setter
    def game_object_layer(self, value: int) -> None: ...

    # =========================================================================
    # Internal (not for user override, but shown for completeness)
    # =========================================================================

    def _set_game_object(self, game_object: GameObject) -> None: ...
    def _serialize_fields(self) -> str: ...
    def _deserialize_fields(self, json_str: str) -> None: ...
    @classmethod
    def _get_type_guid(cls) -> str: ...
    def __repr__(self) -> str: ...
