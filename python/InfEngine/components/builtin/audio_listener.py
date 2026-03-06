"""
AudioListener — Python BuiltinComponent wrapper for C++ AudioListener.

Represents the "ears" in the scene. Typically attached to the main camera.
Only one AudioListener should be active at a time (the last enabled one
takes priority).

Example::

    from InfEngine.components.builtin import AudioListener

    class MainCameraSetup(InfComponent):
        def start(self):
            listener = self.get_component(AudioListener)
            # AudioListener has no configurable properties;
            # its existence on a GameObject registers it as the scene listener.
"""

from __future__ import annotations

from InfEngine.components.builtin_component import BuiltinComponent


class AudioListener(BuiltinComponent):
    """Python wrapper for the C++ AudioListener component.

    The AudioListener marks a GameObject as the scene's audio receiver.
    Attach it to the main camera so that 3D audio is heard from that
    perspective.

    No CppProperty descriptors are needed — the component's presence
    is its only function. Serialization delegates to the C++ component.
    """

    _cpp_type_name = "AudioListener"
    _component_category_ = "Audio"

    # ------------------------------------------------------------------
    # Read-only properties
    # ------------------------------------------------------------------

    @property
    def game_object_id(self) -> int:
        """Owning GameObject ID (for Wwise listener registration)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.game_object_id
        return 0

    # ------------------------------------------------------------------
    # Serialization
    # ------------------------------------------------------------------

    def serialize(self) -> str:
        """Serialize AudioListener to JSON string (delegates to C++)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.serialize()
        return "{}"

    def deserialize(self, json_str: str) -> bool:
        """Deserialize AudioListener from JSON string (delegates to C++)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.deserialize(json_str)
        return False
